#include "engine.h"
#include "heap.h"
#include <stdlib.h>
#include <string.h>
#include <strings.h>

EngineOpts engine_opts = { 1, 1 };

/* ======================================================================== plan */
typedef struct { int slot, col, col2; PredOp op; ColType type; Value lit; int64_t qlo, qhi; char pat[160]; } BPred;
typedef enum { AP_FULL, AP_HASH, AP_BTREE, AP_TRIE, AP_INTERVAL } AccessKind;
typedef struct { AccessKind kind; Index *ix; int64_t lo, hi; char key[160]; } Access;
typedef enum { JN_NONE, JN_INDEX_HASH, JN_INDEX_BTREE, JN_HASH } JoinKind;

typedef struct {
    Table *t[2]; char tname[2][32], alias[2][32]; int nslots;
    BPred preds[MAX_PRED]; int np;
    Access acc[2];
    JoinKind jk; Index *jix; int jcol[2];
} Plan;

typedef struct { int64_t r[2]; } Tup;                         /* one result tuple = one row id per table slot */
typedef struct { Tup *a; size_t n, cap; } TupVec;
static void tv_push(TupVec *v, Tup t) {
    if (v->n == v->cap) { v->cap = v->cap ? v->cap * 2 : 256; v->a = xrealloc(v->a, v->cap * sizeof *v->a); }
    v->a[v->n++] = t;
}
typedef struct { Value g; int64_t count; } GRow;
typedef struct { int kind; int slot, col; char name[72]; } OutCol;   /* kind: 0 column, 1 COUNT(*), 2 group key */

static const Value *cell(const Plan *pl, int slot, int64_t rid, int col) {
    const Table *t = pl->t[slot];
    return &t->data[rid * t->ncols + col];
}

/* ------------------------------------------------------------------- binding */
static int resolve(const Plan *pl, const ColRef *r, int *slot, int *col, char *err, size_t en) {
    int fs = -1, fc = -1;
    for (int s = 0; s < pl->nslots; s++) {
        if (r->tbl[0] && strcasecmp(r->tbl, pl->alias[s]) && strcasecmp(r->tbl, pl->tname[s])) continue;
        int c = table_col(pl->t[s], r->col);
        if (c < 0) continue;
        if (fs >= 0) { snprintf(err, en, "column '%s' is ambiguous; qualify it with a table name", r->col); return 0; }
        fs = s; fc = c;
    }
    if (fs < 0) { snprintf(err, en, "unknown column '%s%s%s'", r->tbl, r->tbl[0] ? "." : "", r->col); return 0; }
    *slot = fs; *col = fc; return 1;
}

static int bind_lit(const Lit *l, ColType ty, Value *out, char *err, size_t en) {
    memset(out, 0, sizeof *out); out->type = (uint8_t)ty; out->s = "";
    if (l->is_null) { snprintf(err, en, "use IS NULL / IS NOT NULL to test for NULL"); return 0; }
    if (ty == T_TEXT) {
        if (!l->is_str) { snprintf(err, en, "TEXT column: compare with a quoted string, not %s", l->s); return 0; }
        out->s = astrdup(l->s); return 1;
    }
    if (ty == T_INT) {
        if (l->is_str) { snprintf(err, en, "INT column: compare with a number, not '%s'", l->s); return 0; }
        out->i = l->n; return 1;
    }
    if (l->is_str) { if (!parse_timestamp(l->s, &out->i)) { snprintf(err, en, "'%s' is not a valid timestamp (use YYYY-MM-DD HH:MM:SS)", l->s); return 0; } }
    else out->i = l->n;
    return 1;
}

static int bind_pred(Plan *pl, const Pred *p, char *err, size_t en) {
    BPred *b = &pl->preds[pl->np];
    memset(b, 0, sizeof *b);
    b->op = p->op; b->col2 = -1;
    if (!resolve(pl, &p->ref, &b->slot, &b->col, err, en)) return 0;
    b->type = pl->t[b->slot]->cols[b->col].type;
    if (p->op == OP_OVERLAP) {
        int s2, c2; Value a, z;
        if (!resolve(pl, &p->ref2, &s2, &c2, err, en)) return 0;
        if (s2 != b->slot) { snprintf(err, en, "OVERLAP needs both columns from the same table"); return 0; }
        if (b->type == T_TEXT || pl->t[s2]->cols[c2].type == T_TEXT) { snprintf(err, en, "OVERLAP needs INT/TIMESTAMP columns"); return 0; }
        if (!bind_lit(&p->lit, b->type, &a, err, en) || !bind_lit(&p->lit2, b->type, &z, err, en)) return 0;
        b->col2 = c2; b->qlo = a.i; b->qhi = z.i;
    } else if (p->op == OP_LIKE) {
        if (b->type != T_TEXT || !p->lit.is_str) { snprintf(err, en, "LIKE needs a TEXT column and a quoted pattern"); return 0; }
        snprintf(b->pat, sizeof b->pat, "%s", p->lit.s);
    } else if (p->op != OP_ISNULL && p->op != OP_NOTNULL) {
        if (!bind_lit(&p->lit, b->type, &b->lit, err, en)) return 0;
    }
    pl->np++;
    return 1;
}

static int eval_pred(const BPred *p, const Value *row) {
    const Value *v = &row[p->col];
    if (p->op == OP_ISNULL) return v->null;
    if (p->op == OP_NOTNULL) return !v->null;
    if (p->op == OP_OVERLAP) {
        if (v->null) return 0;
        int64_t hi = row[p->col2].null ? INT64_MAX : row[p->col2].i;   /* NULL end = still open */
        return v->i <= p->qhi && hi >= p->qlo;
    }
    if (v->null) return 0;
    if (p->op == OP_LIKE) return like_match(p->pat, v->s);
    int c = value_cmp(v, &p->lit);
    switch (p->op) {
    case OP_EQ: return c == 0;  case OP_NE: return c != 0;
    case OP_LT: return c < 0;   case OP_LE: return c <= 0;
    case OP_GT: return c > 0;   case OP_GE: return c >= 0;
    default: return 0;
    }
}

static int row_ok(const Plan *pl, int slot, const Value *row) {
    for (int i = 0; i < pl->np; i++) if (pl->preds[i].slot == slot && !eval_pred(&pl->preds[i], row)) return 0;
    return 1;
}

/* ----------------------------------------------------- rule-based optimizer */
static void choose_access(const Plan *pl, int slot, Access *a) {
    const Table *t = pl->t[slot];
    memset(a, 0, sizeof *a); a->kind = AP_FULL;
    if (!engine_opts.use_indexes) return;
    char kb[32];
    /* rule 1: equality on an indexed column -> hash lookup / B+ tree point probe */
    for (int i = 0; i < pl->np; i++) {
        const BPred *p = &pl->preds[i];
        if (p->slot != slot || p->op != OP_EQ) continue;
        Index *h = table_find_index(t, IX_HASH, p->col, -1);
        if (h) { a->kind = AP_HASH; a->ix = h; snprintf(a->key, sizeof a->key, "%s", value_key(&p->lit, kb, sizeof kb)); return; }
        Index *b = p->type != T_TEXT ? table_find_index(t, IX_BTREE, p->col, -1) : NULL;
        if (b) { a->kind = AP_BTREE; a->ix = b; a->lo = a->hi = p->lit.i; return; }
    }
    /* rule 2: LIKE 'abc%' on a trie-indexed column -> prefix walk */
    for (int i = 0; i < pl->np; i++) {
        const BPred *p = &pl->preds[i];
        if (p->slot != slot || p->op != OP_LIKE) continue;
        size_t L = strlen(p->pat); int ok = L >= 2 && p->pat[L - 1] == '%';
        for (size_t j = 0; ok && j + 1 < L; j++) if (p->pat[j] == '%' || p->pat[j] == '_') ok = 0;
        Index *tr = ok ? table_find_index(t, IX_TRIE, p->col, -1) : NULL;
        if (tr) { a->kind = AP_TRIE; a->ix = tr; memcpy(a->key, p->pat, L - 1); a->key[L - 1] = '\0'; return; }
    }
    /* rule 3: OVERLAP(start, end, a, b) with an interval tree */
    for (int i = 0; i < pl->np; i++) {
        const BPred *p = &pl->preds[i];
        if (p->slot != slot || p->op != OP_OVERLAP) continue;
        Index *it = table_find_index(t, IX_INTERVAL, p->col, p->col2);
        if (it) { a->kind = AP_INTERVAL; a->ix = it; a->lo = p->qlo; a->hi = p->qhi; return; }
    }
    /* rule 4: range / comparison conditions on a B+ tree column. Bounds only need to be a superset:
       every predicate is re-checked on the fetched rows. */
    for (int k = 0; k < t->nidx; k++) {
        Index *ix = t->idx[k];
        if (ix->kind != IX_BTREE) continue;
        int64_t lo = INT64_MIN, hi = INT64_MAX; int used = 0;
        for (int i = 0; i < pl->np; i++) {
            const BPred *p = &pl->preds[i];
            if (p->slot != slot || p->col != ix->col) continue;
            int64_t v = p->lit.i;
            switch (p->op) {
            case OP_EQ: if (v > lo) lo = v; if (v < hi) hi = v; used = 1; break;
            case OP_GT: if (v != INT64_MAX && v + 1 > lo) lo = v + 1; used = 1; break;
            case OP_GE: if (v > lo) lo = v; used = 1; break;
            case OP_LT: if (v != INT64_MIN && v - 1 < hi) hi = v - 1; used = 1; break;
            case OP_LE: if (v < hi) hi = v; used = 1; break;
            default: break;
            }
        }
        if (used) { a->kind = AP_BTREE; a->ix = ix; a->lo = lo; a->hi = hi; return; }
    }
}

static void fetch(const Access *a, RidVec *out) {
    switch (a->kind) {
    case AP_HASH:     { const RidVec *r = hx_find(a->ix->hx, a->key); if (r) for (size_t i = 0; i < r->n; i++) rv_push(out, r->a[i]); break; }
    case AP_BTREE:    bpt_range(a->ix->bt, a->lo, a->hi, out); break;
    case AP_TRIE:     trie_prefix(a->ix->tr, a->key, out); break;
    case AP_INTERVAL: it_overlap(a->ix->it, a->lo, a->hi, out); break;
    case AP_FULL:     break;
    }
}

static void scan_slot(const Plan *pl, int slot, RidVec *out, int64_t *scanned, int64_t stop) {
    const Table *t = pl->t[slot]; const Access *a = &pl->acc[slot];
    if (stop == 0) return;
    if (a->kind == AP_FULL) {
        for (int64_t r = 0; r < t->nrows; r++) {
            (*scanned)++;
            if (row_ok(pl, slot, t->data + r * t->ncols)) { rv_push(out, r); if (stop >= 0 && (int64_t)out->n >= stop) return; }
        }
        return;
    }
    RidVec c; rv_init(&c); fetch(a, &c);
    for (size_t i = 0; i < c.n; i++) {
        (*scanned)++;
        if (row_ok(pl, slot, t->data + c.a[i] * t->ncols)) { rv_push(out, c.a[i]); if (stop >= 0 && (int64_t)out->n >= stop) break; }
    }
    rv_free(&c);
}

/* -------------------------------------------------------------- plan text */
static void bound_str(ColType ty, int64_t v, char *b, size_t n) {
    if (v == INT64_MIN) snprintf(b, n, "-inf");
    else if (v == INT64_MAX) snprintf(b, n, "+inf");
    else if (ty == T_TS) format_timestamp(v, b, n);
    else snprintf(b, n, "%lld", (long long)v);
}

static void access_desc(const Plan *pl, int slot, char *buf, size_t n) {
    const Access *a = &pl->acc[slot]; const Table *t = pl->t[slot];
    char lo[32], hi[32];
    switch (a->kind) {
    case AP_FULL:  snprintf(buf, n, "FULL SCAN of %s (%lld rows)", t->name, (long long)t->nrows); break;
    case AP_HASH:  snprintf(buf, n, "HASH LOOKUP %s.%s = '%s'  using %s", t->name, t->cols[a->ix->col].name, a->key, a->ix->name); break;
    case AP_TRIE:  snprintf(buf, n, "TRIE PREFIX %s.%s LIKE '%s%%'  using %s", t->name, t->cols[a->ix->col].name, a->key, a->ix->name); break;
    case AP_BTREE:
        bound_str(t->cols[a->ix->col].type, a->lo, lo, sizeof lo); bound_str(t->cols[a->ix->col].type, a->hi, hi, sizeof hi);
        snprintf(buf, n, "B+TREE RANGE %s.%s in [%s, %s]  using %s", t->name, t->cols[a->ix->col].name, lo, hi, a->ix->name); break;
    case AP_INTERVAL:
        bound_str(t->cols[a->ix->col].type, a->lo, lo, sizeof lo); bound_str(t->cols[a->ix->col].type, a->hi, hi, sizeof hi);
        snprintf(buf, n, "INTERVAL TREE stab %s(%s,%s) overlaps [%s, %s]  using %s", t->name, t->cols[a->ix->col].name, t->cols[a->ix->col2].name, lo, hi, a->ix->name); break;
    }
}

static const char *ap_short(AccessKind k) {
    switch (k) { case AP_FULL: return "full scan"; case AP_HASH: return "hash index"; case AP_BTREE: return "B+tree";
                 case AP_TRIE: return "trie"; default: return "interval tree"; }
}

static void print_plan(FILE *out, const Plan *pl, const Stmt *st, int agg) {
    char d[400];
    fprintf(out, "QUERY PLAN\n");
    for (int s = 0; s < pl->nslots; s++) {
        int nf = 0; for (int i = 0; i < pl->np; i++) if (pl->preds[i].slot == s) nf++;
        access_desc(pl, s, d, sizeof d);
        if (s == 0) fprintf(out, "  Scan  %s\n", d);
        else if (pl->jk == JN_HASH) fprintf(out, "  Build %s   (then hash it on %s.%s)\n", d, pl->t[1]->name, pl->t[1]->cols[pl->jcol[1]].name);
        else fprintf(out, "  Probe %s via index per left row\n", pl->t[1]->name);
        if (nf) fprintf(out, "        filter: %d condition(s) re-checked on fetched rows\n", nf);
    }
    if (pl->jk == JN_INDEX_HASH)  fprintf(out, "  Join  INDEX NESTED LOOP  %s.%s = %s.%s  probing hash index %s\n", pl->tname[0], pl->t[0]->cols[pl->jcol[0]].name, pl->tname[1], pl->t[1]->cols[pl->jcol[1]].name, pl->jix->name);
    if (pl->jk == JN_INDEX_BTREE) fprintf(out, "  Join  INDEX NESTED LOOP  %s.%s = %s.%s  probing B+tree %s\n", pl->tname[0], pl->t[0]->cols[pl->jcol[0]].name, pl->tname[1], pl->t[1]->cols[pl->jcol[1]].name, pl->jix->name);
    if (pl->jk == JN_HASH)        fprintf(out, "  Join  HASH JOIN  %s.%s = %s.%s  (no usable index: build a hash table at run time)\n", pl->tname[0], pl->t[0]->cols[pl->jcol[0]].name, pl->tname[1], pl->t[1]->cols[pl->jcol[1]].name);
    if (agg) fprintf(out, "  Aggregate  %s COUNT(*) using a hash table\n", st->has_group ? "GROUP BY" : "global");
    if (st->has_order) {
        if (!agg && st->limit >= 0 && engine_opts.use_topk_heap) fprintf(out, "  Sort  TOP-K HEAP (k=%lld): O(n log k), memory O(k)\n", (long long)st->limit);
        else fprintf(out, "  Sort  full sort (qsort): O(n log n)\n");
    }
    if (st->limit >= 0) fprintf(out, "  Limit %lld%s\n", (long long)st->limit, (!agg && !st->has_order) ? "  (scan stops early)" : "");
}

static void plan_summary(const Plan *pl, const Stmt *st, int agg, char *buf, size_t n) {
    size_t o = 0;
    for (int s = 0; s < pl->nslots && o < n; s++) {
        const char *how = ap_short(pl->acc[s].kind);
        if (s == 1 && pl->jk == JN_INDEX_HASH) how = "probe hash index";
        else if (s == 1 && pl->jk == JN_INDEX_BTREE) how = "probe B+tree";
        o += (size_t)snprintf(buf + o, n - o, "%s%s: %s", s ? "; " : "", pl->tname[s], how);
    }
    if (pl->jk != JN_NONE && o < n) o += (size_t)snprintf(buf + o, n - o, "; %s", pl->jk == JN_HASH ? "hash join" : "index nested loop");
    if (agg && o < n) o += (size_t)snprintf(buf + o, n - o, "; hash aggregate");
    if (st->has_order && o < n) snprintf(buf + o, n - o, "; %s", (!agg && st->limit >= 0 && engine_opts.use_topk_heap) ? "top-K heap" : "sort");
}

/* ---------------------------------------------------------------- ordering */
static struct { const Plan *pl; int slot, col, desc; } g_ord;
static int tup_cmp(const void *a, const void *b) {
    const Tup *x = a, *y = b;
    int c = value_cmp(cell(g_ord.pl, g_ord.slot, x->r[g_ord.slot], g_ord.col), cell(g_ord.pl, g_ord.slot, y->r[g_ord.slot], g_ord.col));
    if (g_ord.desc) c = -c;
    if (c) return c;
    if (x->r[0] != y->r[0]) return x->r[0] < y->r[0] ? -1 : 1;     /* deterministic tie-break */
    return x->r[1] < y->r[1] ? -1 : x->r[1] > y->r[1];
}
static int tup_hcmp(const void *a, const void *b, void *ctx) { (void)ctx; return tup_cmp(a, b); }

static struct { int by_count, desc; } g_grp;
static int grow_cmp(const void *a, const void *b) {
    const GRow *x = a, *y = b; int c;
    if (g_grp.by_count) { c = x->count < y->count ? -1 : x->count > y->count; if (g_grp.desc) c = -c; return c ? c : value_cmp(&x->g, &y->g); }
    c = value_cmp(&x->g, &y->g); return g_grp.desc ? -c : c;
}

/* ------------------------------------------------------------------ output */
static void fill_row(const Plan *pl, const OutCol *oc, int noc, const Tup *tp, const GRow *gr, Value *cells) {
    for (int i = 0; i < noc; i++) {
        if (oc[i].kind == 0) cells[i] = *cell(pl, oc[i].slot, tp->r[oc[i].slot], oc[i].col);
        else if (oc[i].kind == 1) { memset(&cells[i], 0, sizeof cells[i]); cells[i].type = T_INT; cells[i].i = gr->count; cells[i].s = ""; }
        else cells[i] = gr->g;
    }
}

static void emit(FILE *out, int print_rows, ExecInfo *info, const Plan *pl, const OutCol *oc, int noc, const Tup *tups, const GRow *groups, size_t n) {
    enum { MAXPRINT = 100 };
    size_t shown = (print_rows && n > MAXPRINT) ? MAXPRINT : (print_rows ? n : 0);
    char **strs = shown ? xmalloc(shown * (size_t)noc * sizeof *strs) : NULL;
    int w[MAX_COLS * 2 + MAX_SEL]; Value cells[MAX_COLS * 2 + MAX_SEL]; char buf[512];
    for (int c = 0; c < noc; c++) w[c] = (int)strlen(oc[c].name);
    for (size_t i = 0; i < n; i++) {
        if (i >= shown && !info->want_checksum) break;
        fill_row(pl, oc, noc, tups ? &tups[i] : NULL, groups ? &groups[i] : NULL, cells);
        uint64_t h = 1469598103934665603ULL;
        for (int c = 0; c < noc; c++) {
            value_str(&cells[c], buf, sizeof buf);
            if (info->want_checksum) { h ^= fnv1a(buf); h *= 1099511628211ULL; }
            if (i < shown) { strs[i * (size_t)noc + (size_t)c] = astrdup(buf); int l = (int)strlen(buf); if (l > w[c]) w[c] = l; }
        }
        if (info->want_checksum) info->checksum += h;
    }
    if (print_rows) {
        for (int c = 0; c < noc; c++) fprintf(out, "%s %-*s ", c ? "|" : "", w[c], oc[c].name);
        fputc('\n', out);
        for (int c = 0; c < noc; c++) { fputs(c ? "+" : "", out); for (int k = 0; k < w[c] + 2; k++) fputc('-', out); }
        fputc('\n', out);
        for (size_t i = 0; i < shown; i++) {
            for (int c = 0; c < noc; c++) fprintf(out, "%s %-*s ", c ? "|" : "", w[c], strs[i * (size_t)noc + (size_t)c]);
            fputc('\n', out);
        }
        if (n > shown) fprintf(out, "... %zu more rows not shown (use \\rows off to hide rows, or LIMIT)\n", n - shown);
    }
    free(strs);
}

/* ---------------------------------------------------------------- SELECT */
static int run_select(Catalog *cat, const Stmt *st, int explain, FILE *out, int print_rows, ExecInfo *info, char *err, size_t en) {
    Plan pl; memset(&pl, 0, sizeof pl);
    pl.t[0] = catalog_find(cat, st->from);
    if (!pl.t[0]) { snprintf(err, en, "unknown table '%s'", st->from); return 0; }
    snprintf(pl.tname[0], 32, "%s", pl.t[0]->name); snprintf(pl.alias[0], 32, "%s", st->from_alias);
    pl.nslots = 1;
    if (st->has_join) {
        pl.t[1] = catalog_find(cat, st->join);
        if (!pl.t[1]) { snprintf(err, en, "unknown table '%s'", st->join); return 0; }
        snprintf(pl.tname[1], 32, "%s", pl.t[1]->name); snprintf(pl.alias[1], 32, "%s", st->join_alias);
        pl.nslots = 2;
        int sa, ca, sb, cb;
        if (!resolve(&pl, &st->on_a, &sa, &ca, err, en) || !resolve(&pl, &st->on_b, &sb, &cb, err, en)) return 0;
        if (sa == sb) { snprintf(err, en, "JOIN ... ON must compare a column of each table"); return 0; }
        if (pl.t[sa]->cols[ca].type != pl.t[sb]->cols[cb].type) { snprintf(err, en, "JOIN columns have different types"); return 0; }
        pl.jcol[sa] = ca; pl.jcol[sb] = cb;
    }
    for (int i = 0; i < st->npreds; i++) if (!bind_pred(&pl, &st->preds[i], err, en)) return 0;

    /* ---- select list ---- */
    int agg = st->has_group, gslot = 0, gcol = 0;
    for (int i = 0; i < st->nsel; i++) if (st->sel[i].kind == SEL_COUNT) agg = 1;
    if (st->has_group && !resolve(&pl, &st->group, &gslot, &gcol, err, en)) return 0;
    OutCol oc[MAX_COLS * 2 + MAX_SEL]; int noc = 0;
    for (int i = 0; i < st->nsel; i++) {
        const SelItem *it = &st->sel[i];
        if (it->kind == SEL_STAR) {
            if (agg) { snprintf(err, en, "SELECT * cannot be combined with COUNT(*) / GROUP BY"); return 0; }
            for (int s = 0; s < pl.nslots; s++) for (int c = 0; c < pl.t[s]->ncols; c++) {
                oc[noc].kind = 0; oc[noc].slot = s; oc[noc].col = c;
                if (pl.nslots > 1) snprintf(oc[noc].name, sizeof oc[noc].name, "%s.%s", pl.alias[s][0] ? pl.alias[s] : pl.tname[s], pl.t[s]->cols[c].name);
                else snprintf(oc[noc].name, sizeof oc[noc].name, "%s", pl.t[s]->cols[c].name);
                noc++;
            }
        } else if (it->kind == SEL_COUNT) { oc[noc].kind = 1; snprintf(oc[noc].name, sizeof oc[noc].name, "count"); noc++; }
        else {
            int s, c; if (!resolve(&pl, &it->ref, &s, &c, err, en)) return 0;
            if (agg && !(st->has_group && s == gslot && c == gcol)) { snprintf(err, en, "column '%s' must appear in GROUP BY or be inside COUNT(*)", it->ref.col); return 0; }
            oc[noc].kind = agg ? 2 : 0; oc[noc].slot = s; oc[noc].col = c;
            snprintf(oc[noc].name, sizeof oc[noc].name, "%s", it->ref.tbl[0] ? it->ref.col : pl.t[s]->cols[c].name);
            noc++;
        }
    }
    int oslot = 0, ocol = 0;
    if (st->has_order && !st->order_count) {
        if (!resolve(&pl, &st->order, &oslot, &ocol, err, en)) return 0;
        if (agg && !(st->has_group && oslot == gslot && ocol == gcol)) { snprintf(err, en, "with GROUP BY, ORDER BY must use the group column or COUNT(*)"); return 0; }
    }
    if (st->order_count && !agg) { snprintf(err, en, "ORDER BY COUNT(*) needs an aggregate query"); return 0; }

    /* ---- optimizer ---- */
    for (int s = 0; s < pl.nslots; s++) choose_access(&pl, s, &pl.acc[s]);
    if (pl.nslots == 2) {
        const Table *rt = pl.t[1]; int rc = pl.jcol[1];
        Index *h = engine_opts.use_indexes ? table_find_index(rt, IX_HASH, rc, -1) : NULL;
        Index *b = (engine_opts.use_indexes && rt->cols[rc].type != T_TEXT) ? table_find_index(rt, IX_BTREE, rc, -1) : NULL;
        if (h) { pl.jk = JN_INDEX_HASH; pl.jix = h; } else if (b) { pl.jk = JN_INDEX_BTREE; pl.jix = b; } else pl.jk = JN_HASH;
    }
    char summary[512]; plan_summary(&pl, st, agg, summary, sizeof summary);
    if (info) snprintf(info->plan, sizeof info->plan, "%s", summary);
    if (explain) { print_plan(out, &pl, st, agg); return 1; }

    /* ---- execute ---- */
    double t0 = now_ms();
    int64_t scanned = 0;
    int64_t stop = (!agg && !st->has_order && st->limit >= 0) ? st->limit : -1;
    TupVec tv = { 0 };
    if (pl.nslots == 1) {
        RidVec r; rv_init(&r); scan_slot(&pl, 0, &r, &scanned, stop);
        for (size_t i = 0; i < r.n; i++) { Tup t = { { r.a[i], -1 } }; tv_push(&tv, t); }
        rv_free(&r);
    } else {
        RidVec L; rv_init(&L); scan_slot(&pl, 0, &L, &scanned, -1);
        HashIdx *rh = NULL; char kb[32];
        if (pl.jk == JN_HASH) {                                       /* build side = filtered right table */
            RidVec R; rv_init(&R); scan_slot(&pl, 1, &R, &scanned, -1);
            rh = hx_new();
            for (size_t i = 0; i < R.n; i++) { const Value *v = cell(&pl, 1, R.a[i], pl.jcol[1]); if (!v->null) hx_insert(rh, value_key(v, kb, sizeof kb), R.a[i]); }
            rv_free(&R);
        }
        RidVec tmp; rv_init(&tmp);
        for (size_t i = 0; i < L.n && !(stop >= 0 && (int64_t)tv.n >= stop); i++) {
            const Value *lv = cell(&pl, 0, L.a[i], pl.jcol[0]);
            if (lv->null) continue;
            const int64_t *cand = NULL; size_t nc = 0; int need_check = 1;
            tmp.n = 0;
            if (pl.jk == JN_INDEX_BTREE) { bpt_range(pl.jix->bt, lv->i, lv->i, &tmp); cand = tmp.a; nc = tmp.n; }
            else {
                const RidVec *rr = hx_find(pl.jk == JN_HASH ? rh : pl.jix->hx, value_key(lv, kb, sizeof kb));
                if (rr) { cand = rr->a; nc = rr->n; }
                if (pl.jk == JN_HASH) need_check = 0;                 /* build side already filtered */
            }
            for (size_t k = 0; k < nc; k++) {
                if (need_check) { scanned++; if (!row_ok(&pl, 1, pl.t[1]->data + cand[k] * pl.t[1]->ncols)) continue; }
                Tup t = { { L.a[i], cand[k] } }; tv_push(&tv, t);
                if (stop >= 0 && (int64_t)tv.n >= stop) break;
            }
        }
        rv_free(&tmp); rv_free(&L); hx_free(rh);
    }

    GRow *groups = NULL; size_t ng = 0;
    if (agg) {
        if (!st->has_group) { groups = xcalloc(1, sizeof *groups); groups[0].g.null = 1; groups[0].count = (int64_t)tv.n; ng = 1; }
        else {
            HashIdx *gh = hx_new(); char kb[32];
            for (size_t i = 0; i < tv.n; i++) {
                const Value *v = cell(&pl, gslot, tv.a[i].r[gslot], gcol);
                hx_insert(gh, v->null ? "\x01NULL" : value_key(v, kb, sizeof kb), (int64_t)i);
            }
            HNode **nodes; ng = hx_collect(gh, &nodes);
            groups = xmalloc((ng ? ng : 1) * sizeof *groups);
            for (size_t i = 0; i < ng; i++) { groups[i].g = *cell(&pl, gslot, tv.a[nodes[i]->rids.a[0]].r[gslot], gcol); groups[i].count = (int64_t)nodes[i]->rids.n; }
            free(nodes); hx_free(gh);
            g_grp.by_count = st->order_count; g_grp.desc = st->has_order ? st->order_desc : 0;
            qsort(groups, ng, sizeof *groups, grow_cmp);
        }
        if (st->limit >= 0 && (size_t)st->limit < ng) ng = (size_t)st->limit;
    } else if (st->has_order && tv.n) {
        g_ord.pl = &pl; g_ord.slot = oslot; g_ord.col = ocol; g_ord.desc = st->order_desc;
        if (st->limit >= 0 && engine_opts.use_topk_heap) {            /* top-K with a bounded max-heap */
            size_t K = (size_t)st->limit; Heap h; heap_init(&h, tup_hcmp, NULL);
            for (size_t i = 0; i < tv.n; i++) { heap_push(&h, &tv.a[i]); if (h.n > K) heap_pop(&h); }
            size_t m = h.n; Tup *res = xmalloc((m ? m : 1) * sizeof *res);
            for (size_t j = m; j > 0; j--) res[j - 1] = *(Tup *)heap_pop(&h);
            heap_free(&h); free(tv.a); tv.a = res; tv.n = m; tv.cap = m;
        } else {
            qsort(tv.a, tv.n, sizeof *tv.a, tup_cmp);
            if (st->limit >= 0 && (size_t)st->limit < tv.n) tv.n = (size_t)st->limit;
        }
    } else if (st->limit >= 0 && (size_t)st->limit < tv.n) tv.n = (size_t)st->limit;

    size_t nrows = agg ? ng : tv.n;
    double ms = now_ms() - t0;
    ExecInfo local = { 0 }; ExecInfo *inf = info ? info : &local;
    inf->rows = (int64_t)nrows; inf->scanned = scanned; inf->ms = ms; inf->checksum = 0;
    emit(out, print_rows, inf, &pl, oc, noc, agg ? NULL : tv.a, agg ? groups : NULL, nrows);
    fprintf(out, "(%zu row%s)   [%s | scanned %lld rows | %.3f ms]\n", nrows, nrows == 1 ? "" : "s", summary, (long long)scanned, ms);
    free(tv.a); free(groups);
    return 1;
}

/* ------------------------------------------------------- other statements */
static int run_create_index(Catalog *cat, const Stmt *st, FILE *out, char *err, size_t en) {
    Table *t = catalog_find(cat, st->table);
    if (!t) { snprintf(err, en, "unknown table '%s'", st->table); return 0; }
    int col = table_col(t, st->col), col2 = st->col2[0] ? table_col(t, st->col2) : -1;
    if (col < 0) { snprintf(err, en, "unknown column '%s'", st->col); return 0; }
    if (st->col2[0] && col2 < 0) { snprintf(err, en, "unknown column '%s'", st->col2); return 0; }
    IndexKind k;
    if (st->ix_kind >= 0) k = (IndexKind)st->ix_kind;
    else if (col2 >= 0) k = IX_INTERVAL;
    else k = t->cols[col].type == T_TEXT ? IX_HASH : IX_BTREE;
    if (col2 >= 0 && k != IX_INTERVAL) { snprintf(err, en, "only INTERVAL indexes take two columns"); return 0; }
    char name[80];
    if (st->name[0]) snprintf(name, sizeof name, "%s", st->name);
    else {   /* default name carries the kind, so BTREE and INTERVAL indexes on one column do not collide */
        static const char *kn[] = { "btree", "hash", "trie", "interval" };
        snprintf(name, sizeof name, "idx_%.24s_%.24s_%s", t->name, t->cols[col].name, kn[k]);
    }
    Index *ix = table_create_index(t, name, k, col, col2, err, en);
    if (!ix) return 0;
    char d[256]; index_describe(ix, t, d, sizeof d);
    fprintf(out, "CREATE INDEX: %s\n  built in %.2f ms\n", d, ix->build_ms);
    return 1;
}

static int run_create_table(Catalog *cat, const Stmt *st, FILE *out, char *err, size_t en) {
    Table *t = catalog_create_table(cat, st->table, st->create_cols, st->ncreate_cols, err, en);
    if (!t) return 0;
    fprintf(out, "CREATE TABLE %s (%d column%s)\n", t->name, t->ncols, t->ncols == 1 ? "" : "s");
    return 1;
}

static int run_insert(Catalog *cat, const Stmt *st, FILE *out, char *err, size_t en) {
    Table *t = catalog_find(cat, st->table);
    if (!t) { snprintf(err, en, "unknown table '%s'", st->table); return 0; }
    if (st->nvals != t->ncols) { snprintf(err, en, "%s has %d columns, got %d values", t->name, t->ncols, st->nvals); return 0; }
    Value row[MAX_COLS];
    for (int c = 0; c < t->ncols; c++) {
        if (st->vals[c].is_null) { memset(&row[c], 0, sizeof row[c]); row[c].type = (uint8_t)t->cols[c].type; row[c].null = 1; row[c].s = ""; }
        else if (!bind_lit(&st->vals[c], t->cols[c].type, &row[c], err, en)) return 0;
    }
    table_append(t, row);
    fprintf(out, "INSERT 1  (%s now has %lld rows; %d index(es) updated)\n", t->name, (long long)t->nrows, t->nidx);
    return 1;
}

void engine_help(FILE *out) {
    fputs(
"Statements (end with ;)\n"
"  CREATE TABLE <table> (col type, ...);               types: INT, TEXT, TIMESTAMP\n"
"  LOAD <table> FROM 'file.csv';                       bulk-load a CSV exported from PostgreSQL\n"
"  CREATE INDEX [name] ON t (col) [USING BTREE|HASH|TRIE];\n"
"  CREATE INDEX [name] ON t (start_col, end_col) USING INTERVAL;\n"
"  DROP INDEX name;      SHOW TABLES;      SHOW INDEXES;      DESCRIBE t;\n"
"  INSERT INTO t VALUES (v1, v2, ...);                 values in DESCRIBE order; NULL allowed\n"
"  [EXPLAIN] SELECT * | col, ... | COUNT(*) FROM t [alias]\n"
"      [JOIN t2 [alias] ON a.x = b.y]\n"
"      [WHERE cond AND cond ...]                       cond: col (= != < <= > >=) literal\n"
"                                                            col LIKE 'text%'   (case-insensitive)\n"
"                                                            col IS [NOT] NULL\n"
"                                                            OVERLAP(start_col, end_col, 'from', 'to')\n"
"      [GROUP BY col] [ORDER BY col|COUNT(*) [ASC|DESC]] [LIMIT n];\n"
"Shell commands:  \\i file.sql   \\rows on|off   \\opt indexes on|off   \\opt heap on|off   \\q\n"
"Timestamps are UTC. Not supported: OR, subqueries, UPDATE, DELETE, outer joins.\n", out);
}

int engine_exec(Catalog *cat, const Stmt *st, FILE *out, int print_rows, ExecInfo *info, char *err, size_t en) {
    err[0] = '\0';
    switch (st->kind) {
    case ST_SELECT:  return run_select(cat, st, 0, out, print_rows, info, err, en);
    case ST_EXPLAIN: return run_select(cat, st, 1, out, print_rows, info, err, en);
    case ST_CREATE_INDEX: return run_create_index(cat, st, out, err, en);
    case ST_CREATE_TABLE: return run_create_table(cat, st, out, err, en);
    case ST_INSERT:  return run_insert(cat, st, out, err, en);
    case ST_DROP_INDEX:
        for (int i = 0; i < cat->n; i++) if (table_drop_index(cat->t[i], st->name)) { fprintf(out, "DROP INDEX %s\n", st->name); return 1; }
        snprintf(err, en, "no such index '%s'", st->name); return 0;
    case ST_LOAD: {
        Table *t = catalog_find(cat, st->table);
        if (!t) { snprintf(err, en, "unknown table '%s'", st->table); return 0; }
        int64_t n; double t0 = now_ms();
        int ok = table_load_csv(t, st->path, &n, err, en);
        fprintf(out, "LOAD %s: %lld rows in %.1f ms (table now has %lld rows, %d index(es) maintained)\n", t->name, (long long)n, now_ms() - t0, (long long)t->nrows, t->nidx);
        return ok;
    }
    case ST_SHOW_TABLES:
        fprintf(out, "%-16s %7s %12s\n", "table", "columns", "rows");
        for (int i = 0; i < cat->n; i++) fprintf(out, "%-16s %7d %12lld\n", cat->t[i]->name, cat->t[i]->ncols, (long long)cat->t[i]->nrows);
        return 1;
    case ST_SHOW_INDEXES: {
        int any = 0; char d[300];
        for (int i = 0; i < cat->n; i++) for (int k = 0; k < cat->t[i]->nidx; k++) { index_describe(cat->t[i]->idx[k], cat->t[i], d, sizeof d); fprintf(out, "%s\n", d); any = 1; }
        if (!any) fprintf(out, "(no indexes)\n");
        return 1;
    }
    case ST_DESCRIBE: {
        Table *t = catalog_find(cat, st->table);
        if (!t) { snprintf(err, en, "unknown table '%s'", st->table); return 0; }
        fprintf(out, "%s  (%lld rows)\n", t->name, (long long)t->nrows);
        for (int c = 0; c < t->ncols; c++) fprintf(out, "  %-20s %s\n", t->cols[c].name, coltype_name(t->cols[c].type));
        return 1;
    }
    case ST_HELP: engine_help(out); return 1;
    default: return 1;
    }
}
