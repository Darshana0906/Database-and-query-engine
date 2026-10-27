#include "table.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

/* ---- built-in schemas: a column subset of the PostgreSQL tables; extra CSV columns are ignored ---- */
typedef struct { const char *name; const char *cols; } Schema;
static const Schema SCHEMAS[] = {
    { "hostel",         "id:int name:text address:text gate_closure_time:text" },
    { "in_out_time",    "id:int hostel_id:int out_time:text in_time:text" },
    { "warden",         "id:int username:text name:text mobile_no:text hostel_id:int" },
    { "student",        "mis:text name:text class:text room:text home_address:text admission_date:ts phone_no:text parents_phone_no:text hostel_id:int" },
    { "entry_exit_log", "id:int student_mis:text in_out_time_id:int log_date:ts exit_time:ts entry_time:ts entry_distance_m:int status:text" },
    { "alert",          "alert_id:int log_id:int warden_id:int alert_type:text alert_time:ts message:text status:text" },
};

void catalog_init(Catalog *c) {
    memset(c, 0, sizeof *c);
    for (size_t s = 0; s < sizeof SCHEMAS / sizeof *SCHEMAS; s++) {
        Table *t = xcalloc(1, sizeof *t);
        snprintf(t->name, sizeof t->name, "%s", SCHEMAS[s].name);
        char buf[512]; snprintf(buf, sizeof buf, "%s", SCHEMAS[s].cols);
        for (char *tok = strtok(buf, " "); tok; tok = strtok(NULL, " ")) {
            char *colon = strchr(tok, ':'); *colon = '\0';
            Column *cl = &t->cols[t->ncols++];
            snprintf(cl->name, sizeof cl->name, "%s", tok);
            cl->type = !strcmp(colon + 1, "int") ? T_INT : !strcmp(colon + 1, "ts") ? T_TS : T_TEXT;
        }
        c->t[c->n++] = t;
    }
}

Table *catalog_find(const Catalog *c, const char *name) {
    for (int i = 0; i < c->n; i++) if (!strcasecmp(c->t[i]->name, name)) return c->t[i];
    return NULL;
}
int table_col(const Table *t, const char *name) {
    for (int i = 0; i < t->ncols; i++) if (!strcasecmp(t->cols[i].name, name)) return i;
    return -1;
}
const char *coltype_name(ColType t) { return t == T_INT ? "INT" : t == T_TS ? "TIMESTAMP" : "TEXT"; }
const char *indexkind_name(IndexKind k) {
    switch (k) { case IX_BTREE: return "BTREE"; case IX_HASH: return "HASH"; case IX_TRIE: return "TRIE"; default: return "INTERVAL"; }
}

/* ---- index maintenance ---- */
static void index_add_row(Index *ix, const Table *t, int64_t rid) {
    const Value *row = t->data + rid * t->ncols;
    const Value *v = &row[ix->col];
    char kb[32];
    if (v->null) return;                                   /* NULL keys are not indexed */
    switch (ix->kind) {
    case IX_BTREE:    bpt_insert(ix->bt, v->i, rid); break;
    case IX_HASH:     hx_insert(ix->hx, value_key(v, kb, sizeof kb), rid); break;
    case IX_TRIE:     trie_insert(ix->tr, v->s, rid); break;
    case IX_INTERVAL: { const Value *e = &row[ix->col2]; it_insert(ix->it, v->i, e->null ? INT64_MAX : e->i, rid); break; }
    }
    ix->entries++;
}

Index *table_find_index(const Table *t, IndexKind k, int col, int col2) {
    for (int i = 0; i < t->nidx; i++) {
        Index *ix = t->idx[i];
        if (ix->kind == k && ix->col == col && (k != IX_INTERVAL || ix->col2 == col2)) return ix;
    }
    return NULL;
}

Index *table_create_index(Table *t, const char *name, IndexKind k, int col, int col2, char *err, size_t en) {
    if (t->nidx >= MAX_IDX) { snprintf(err, en, "too many indexes on %s", t->name); return NULL; }
    for (int i = 0; i < t->nidx; i++) if (!strcasecmp(t->idx[i]->name, name)) { snprintf(err, en, "index '%s' already exists", name); return NULL; }
    ColType ct = t->cols[col].type;
    if (k == IX_BTREE && ct == T_TEXT)        { snprintf(err, en, "BTREE indexes need an INT or TIMESTAMP column (use HASH or TRIE for text)"); return NULL; }
    if (k == IX_TRIE && ct != T_TEXT)         { snprintf(err, en, "TRIE indexes need a TEXT column"); return NULL; }
    if (k == IX_INTERVAL) {
        if (col2 < 0 || ct == T_TEXT || t->cols[col2].type == T_TEXT) { snprintf(err, en, "INTERVAL index needs two INT/TIMESTAMP columns: (start, end)"); return NULL; }
    }
    Index *ix = xcalloc(1, sizeof *ix);
    snprintf(ix->name, sizeof ix->name, "%s", name);
    ix->kind = k; ix->col = col; ix->col2 = k == IX_INTERVAL ? col2 : -1;
    switch (k) { case IX_BTREE: ix->bt = bpt_new(); break; case IX_HASH: ix->hx = hx_new(); break;
                 case IX_TRIE: ix->tr = trie_new(); break; case IX_INTERVAL: ix->it = it_new(); break; }
    double t0 = now_ms();
    for (int64_t r = 0; r < t->nrows; r++) index_add_row(ix, t, r);
    ix->build_ms = now_ms() - t0;
    t->idx[t->nidx++] = ix;
    return ix;
}

int table_drop_index(Table *t, const char *name) {
    for (int i = 0; i < t->nidx; i++) if (!strcasecmp(t->idx[i]->name, name)) {
        Index *ix = t->idx[i];
        bpt_free(ix->bt); hx_free(ix->hx); trie_free(ix->tr); it_free(ix->it); free(ix);
        memmove(&t->idx[i], &t->idx[i + 1], (size_t)(t->nidx - i - 1) * sizeof ix);
        t->nidx--; return 1;
    }
    return 0;
}

void index_describe(const Index *ix, const Table *t, char *buf, size_t n) {
    char extra[96] = "";
    switch (ix->kind) {
    case IX_BTREE:    snprintf(extra, sizeof extra, "height %d, %zu nodes", bpt_height(ix->bt), bpt_nodes(ix->bt)); break;
    case IX_HASH:     snprintf(extra, sizeof extra, "%zu keys, %zu buckets, longest chain %zu", ix->hx->nkeys, ix->hx->nb, hx_max_chain(ix->hx)); break;
    case IX_TRIE:     snprintf(extra, sizeof extra, "%zu nodes", trie_nodes(ix->tr)); break;
    case IX_INTERVAL: snprintf(extra, sizeof extra, "treap height %d", it_height(ix->it)); break;
    }
    if (ix->kind == IX_INTERVAL)
        snprintf(buf, n, "%-22s %-15s %-9s (%s, %s)  %lld entries, %s", ix->name, t->name, indexkind_name(ix->kind), t->cols[ix->col].name, t->cols[ix->col2].name, (long long)ix->entries, extra);
    else
        snprintf(buf, n, "%-22s %-15s %-9s (%s)  %lld entries, %s", ix->name, t->name, indexkind_name(ix->kind), t->cols[ix->col].name, (long long)ix->entries, extra);
}

/* ---- row append ---- */
void table_append(Table *t, const Value *row) {
    if (t->nrows == t->cap) { t->cap = t->cap ? t->cap * 2 : 1024; t->data = xrealloc(t->data, (size_t)t->cap * (size_t)t->ncols * sizeof(Value)); }
    memcpy(t->data + t->nrows * t->ncols, row, (size_t)t->ncols * sizeof(Value));
    for (int i = 0; i < t->nidx; i++) index_add_row(t->idx[i], t, t->nrows);
    t->nrows++;
}

/* ---- CSV reader (RFC 4180: quoted fields, "" escapes, embedded commas/newlines, CRLF) ---- */
typedef struct { char **f; int n, cap; char *buf; size_t len, bcap; size_t *off; } Csv;

static void bput(Csv *r, char c) {
    if (r->len == r->bcap) { r->bcap = r->bcap ? r->bcap * 2 : 1024; r->buf = xrealloc(r->buf, r->bcap); }
    r->buf[r->len++] = c;
}

static int csv_read(FILE *fp, Csv *r) {
    int c = getc(fp);
    if (c == EOF) return 0;
    r->n = 0; r->len = 0;
    for (;;) {
        if (r->n == r->cap) { r->cap = r->cap ? r->cap * 2 : 16; r->f = xrealloc(r->f, (size_t)r->cap * sizeof *r->f); r->off = xrealloc(r->off, (size_t)r->cap * sizeof *r->off); }
        r->off[r->n++] = r->len;
        if (c == '"') {
            for (;;) {
                c = getc(fp);
                if (c == EOF) break;
                if (c == '"') { int nx = getc(fp); if (nx == '"') bput(r, '"'); else { c = nx; break; } }
                else bput(r, (char)c);
            }
        } else {
            while (c != ',' && c != '\n' && c != EOF) { if (c != '\r') bput(r, (char)c); c = getc(fp); }
        }
        bput(r, '\0');
        while (c == '\r') c = getc(fp);
        if (c == ',') { c = getc(fp); continue; }
        break;                                            /* '\n' or EOF ends the record */
    }
    for (int i = 0; i < r->n; i++) r->f[i] = r->buf + r->off[i];
    return 1;
}

int table_load_csv(Table *t, const char *path, int64_t *loaded, char *err, size_t en) {
    FILE *fp = fopen(path, "rb");
    *loaded = 0;
    if (!fp) { snprintf(err, en, "cannot open '%s'", path); return 0; }
    static Csv r;                                         /* reused buffers */
    if (!csv_read(fp, &r)) { fclose(fp); snprintf(err, en, "'%s' is empty", path); return 0; }
    if ((unsigned char)r.f[0][0] == 0xEF && (unsigned char)r.f[0][1] == 0xBB) memmove(r.f[0], r.f[0] + 3, strlen(r.f[0] + 3) + 1); /* UTF-8 BOM */
    int map[MAX_COLS];
    for (int c = 0; c < t->ncols; c++) {
        map[c] = -1;
        for (int j = 0; j < r.n; j++) if (!strcasecmp(r.f[j], t->cols[c].name)) map[c] = j;
        if (map[c] < 0) { fclose(fp); snprintf(err, en, "CSV header is missing column '%s'", t->cols[c].name); return 0; }
    }
    Value row[MAX_COLS];
    long line = 1;
    while (csv_read(fp, &r)) {
        line++;
        if (r.n == 1 && r.f[0][0] == '\0') continue;       /* blank line */
        for (int c = 0; c < t->ncols; c++) {
            const char *f = map[c] < r.n ? r.f[map[c]] : "";
            Value *v = &row[c];
            v->type = (uint8_t)t->cols[c].type; v->i = 0; v->s = ""; v->null = 0;
            if (!*f) { v->null = 1; continue; }            /* empty field = NULL */
            if (v->type == T_TEXT) v->s = astrdup(f);
            else if (v->type == T_INT) {
                char *e; long long x = strtoll(f, &e, 10);
                if (*e) { fclose(fp); snprintf(err, en, "line %ld: '%s' is not an integer (column %s)", line, f, t->cols[c].name); return 0; }
                v->i = x;
            } else if (!parse_timestamp(f, &v->i)) {
                fclose(fp); snprintf(err, en, "line %ld: '%s' is not a timestamp (column %s)", line, f, t->cols[c].name); return 0;
            }
        }
        table_append(t, row);
        (*loaded)++;
    }
    fclose(fp);
    return 1;
}
