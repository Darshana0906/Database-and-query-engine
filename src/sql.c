#include "sql.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>

typedef enum { TK_END, TK_ID, TK_NUM, TK_STR, TK_SYM } TkKind;
typedef struct { TkKind k; char s[160]; int64_t n; } Tok;
#define MAX_TOK 400
typedef struct { Tok t[MAX_TOK]; int n, pos; char *err; size_t en; } P;

/* ------------------------------------------------------------------ lexer */
static int lex(const char *src, P *p) {
    const char *s = src; p->n = 0; p->pos = 0;
    while (1) {
        while (isspace((unsigned char)*s)) s++;
        if (p->n >= MAX_TOK - 1) { snprintf(p->err, p->en, "statement too long"); return 0; }
        Tok *t = &p->t[p->n];
        t->s[0] = '\0'; t->n = 0;
        if (!*s) { t->k = TK_END; p->n++; return 1; }
        if (s[0] == '-' && s[1] == '-') { while (*s && *s != '\n') s++; continue; }
        if (isalpha((unsigned char)*s) || *s == '_') {
            size_t i = 0;
            while ((isalnum((unsigned char)*s) || *s == '_') && i < sizeof t->s - 1) t->s[i++] = *s++;
            t->s[i] = '\0'; t->k = TK_ID;
        } else if (isdigit((unsigned char)*s) || (*s == '-' && isdigit((unsigned char)s[1]))) {
            char *e; t->n = strtoll(s, &e, 10);
            size_t len = (size_t)(e - s); if (len >= sizeof t->s) len = sizeof t->s - 1;
            memcpy(t->s, s, len); t->s[len] = '\0'; s = e; t->k = TK_NUM;
        } else if (*s == '\'') {
            s++; size_t i = 0;
            for (;;) {
                if (!*s) { snprintf(p->err, p->en, "unterminated string literal"); return 0; }
                if (*s == '\'') { if (s[1] == '\'') { if (i < sizeof t->s - 1) t->s[i++] = '\''; s += 2; continue; } s++; break; }
                if (i < sizeof t->s - 1) t->s[i++] = *s;
                s++;
            }
            t->s[i] = '\0'; t->k = TK_STR;
        } else {
            t->k = TK_SYM;
            if ((s[0] == '<' && (s[1] == '=' || s[1] == '>')) || (s[0] == '>' && s[1] == '=') || (s[0] == '!' && s[1] == '=')) { t->s[0] = s[0]; t->s[1] = s[1]; t->s[2] = '\0'; s += 2; }
            else if (strchr(",()*=;.<>", *s)) { t->s[0] = *s++; t->s[1] = '\0'; }
            else { snprintf(p->err, p->en, "unexpected character '%c'", *s); return 0; }
        }
        p->n++;
    }
}

/* ---------------------------------------------------------------- helpers */
#define FAIL(...) do { snprintf(p->err, p->en, __VA_ARGS__); return 0; } while (0)
static Tok *peek(P *p) { return &p->t[p->pos]; }
static Tok *take(P *p) { Tok *t = &p->t[p->pos]; if (t->k != TK_END) p->pos++; return t; }
static int is_kw(const Tok *t, const char *kw) { return t->k == TK_ID && !strcasecmp(t->s, kw); }
static int accept_kw(P *p, const char *kw) { if (is_kw(peek(p), kw)) { p->pos++; return 1; } return 0; }
static int is_sym(const Tok *t, const char *s) { return t->k == TK_SYM && !strcmp(t->s, s); }
static int accept_sym(P *p, const char *s) { if (is_sym(peek(p), s)) { p->pos++; return 1; } return 0; }
static const char *near_txt(P *p) { Tok *t = peek(p); return t->k == TK_END ? "end of statement" : t->s; }
static int expect_kw(P *p, const char *kw) { if (accept_kw(p, kw)) return 1; FAIL("syntax error: expected %s near '%s'", kw, near_txt(p)); }
static int expect_sym(P *p, const char *s) { if (accept_sym(p, s)) return 1; FAIL("syntax error: expected '%s' near '%s'", s, near_txt(p)); }

static int ident(P *p, char *out, size_t n) {
    Tok *t = peek(p);
    if (t->k != TK_ID) FAIL("syntax error: expected a name near '%s'", near_txt(p));
    snprintf(out, n, "%s", t->s); p->pos++; return 1;
}

static int colref(P *p, ColRef *r) {
    char a[32];
    r->tbl[0] = r->col[0] = '\0';
    if (!ident(p, a, sizeof a)) return 0;
    if (accept_sym(p, ".")) { snprintf(r->tbl, sizeof r->tbl, "%s", a); return ident(p, r->col, sizeof r->col); }
    snprintf(r->col, sizeof r->col, "%s", a);
    return 1;
}

static int literal(P *p, Lit *l) {
    Tok *t = peek(p);
    memset(l, 0, sizeof *l);
    if (t->k == TK_NUM) { l->n = t->n; snprintf(l->s, sizeof l->s, "%s", t->s); p->pos++; return 1; }
    if (t->k == TK_STR) { l->is_str = 1; snprintf(l->s, sizeof l->s, "%s", t->s); p->pos++; return 1; }
    if (is_kw(t, "NULL")) { l->is_null = 1; p->pos++; return 1; }
    FAIL("syntax error: expected a literal near '%s'", near_txt(p));
}

static int is_reserved(const char *s) {
    static const char *kw[] = { "JOIN", "INNER", "WHERE", "GROUP", "ORDER", "LIMIT", "ON", "AND", "USING" };
    for (size_t i = 0; i < sizeof kw / sizeof *kw; i++) if (!strcasecmp(s, kw[i])) return 1;
    return 0;
}

/* table [AS] [alias] */
static int table_ref(P *p, char *tbl, char *alias) {
    alias[0] = '\0';
    if (!ident(p, tbl, 32)) return 0;
    if (accept_kw(p, "AS")) return ident(p, alias, 32);
    if (peek(p)->k == TK_ID && !is_reserved(peek(p)->s)) return ident(p, alias, 32);
    return 1;
}

/* ----------------------------------------------------------------- SELECT */
static int predicate(P *p, Pred *pr) {
    memset(pr, 0, sizeof *pr);
    if (is_kw(peek(p), "OVERLAP")) {
        p->pos++; pr->op = OP_OVERLAP;
        if (!expect_sym(p, "(") || !colref(p, &pr->ref) || !expect_sym(p, ",") || !colref(p, &pr->ref2) || !expect_sym(p, ",")
            || !literal(p, &pr->lit) || !expect_sym(p, ",") || !literal(p, &pr->lit2) || !expect_sym(p, ")")) return 0;
        return 1;
    }
    if (!colref(p, &pr->ref)) return 0;
    if (accept_kw(p, "IS")) {
        pr->op = accept_kw(p, "NOT") ? OP_NOTNULL : OP_ISNULL;
        return expect_kw(p, "NULL");
    }
    if (accept_kw(p, "LIKE")) { pr->op = OP_LIKE; return literal(p, &pr->lit); }
    Tok *t = peek(p);
    if (t->k != TK_SYM) FAIL("syntax error: expected a comparison operator near '%s'", near_txt(p));
    if      (!strcmp(t->s, "="))  pr->op = OP_EQ;
    else if (!strcmp(t->s, "!=") || !strcmp(t->s, "<>")) pr->op = OP_NE;
    else if (!strcmp(t->s, "<"))  pr->op = OP_LT;
    else if (!strcmp(t->s, "<=")) pr->op = OP_LE;
    else if (!strcmp(t->s, ">"))  pr->op = OP_GT;
    else if (!strcmp(t->s, ">=")) pr->op = OP_GE;
    else FAIL("syntax error: expected a comparison operator near '%s'", t->s);
    p->pos++;
    return literal(p, &pr->lit);
}

static int select_list(P *p, Stmt *st) {
    do {
        if (st->nsel >= MAX_SEL) FAIL("too many select items");
        SelItem *it = &st->sel[st->nsel];
        if (accept_sym(p, "*")) it->kind = SEL_STAR;
        else if (is_kw(peek(p), "COUNT")) {
            p->pos++; it->kind = SEL_COUNT;
            if (!expect_sym(p, "(") || !expect_sym(p, "*") || !expect_sym(p, ")")) return 0;
        } else { it->kind = SEL_COL; if (!colref(p, &it->ref)) return 0; }
        st->nsel++;
    } while (accept_sym(p, ","));
    return 1;
}

static int parse_select(P *p, Stmt *st) {
    st->limit = -1;
    if (!select_list(p, st) || !expect_kw(p, "FROM") || !table_ref(p, st->from, st->from_alias)) return 0;
    accept_kw(p, "INNER");
    if (accept_kw(p, "JOIN")) {
        st->has_join = 1;
        if (!table_ref(p, st->join, st->join_alias) || !expect_kw(p, "ON") || !colref(p, &st->on_a) || !expect_sym(p, "=") || !colref(p, &st->on_b)) return 0;
    }
    if (accept_kw(p, "WHERE")) {
        do {
            if (st->npreds >= MAX_PRED) FAIL("too many WHERE conditions (max %d)", MAX_PRED);
            if (!predicate(p, &st->preds[st->npreds++])) return 0;
        } while (accept_kw(p, "AND"));
        if (is_kw(peek(p), "OR")) FAIL("OR is not supported; conditions must be joined with AND");
    }
    if (accept_kw(p, "GROUP")) { if (!expect_kw(p, "BY") || !colref(p, &st->group)) return 0; st->has_group = 1; }
    if (accept_kw(p, "ORDER")) {
        if (!expect_kw(p, "BY")) return 0;
        st->has_order = 1;
        if (is_kw(peek(p), "COUNT")) { p->pos++; st->order_count = 1; if (!expect_sym(p, "(") || !expect_sym(p, "*") || !expect_sym(p, ")")) return 0; }
        else if (!colref(p, &st->order)) return 0;
        if (accept_kw(p, "DESC")) st->order_desc = 1; else accept_kw(p, "ASC");
    }
    if (accept_kw(p, "LIMIT")) {
        Tok *t = take(p);
        if (t->k != TK_NUM || t->n < 0) FAIL("syntax error: LIMIT needs a non-negative number");
        st->limit = t->n;
    }
    return 1;
}

/* ------------------------------------------------------------- statements */
static int parse_stmt(P *p, Stmt *st) {
    Tok *t = peek(p);
    if (t->k == TK_END || is_sym(t, ";")) { st->kind = ST_EMPTY; return 1; }
    if (accept_kw(p, "SELECT")) { st->kind = ST_SELECT; return parse_select(p, st); }
    if (accept_kw(p, "EXPLAIN")) { st->kind = ST_EXPLAIN; return expect_kw(p, "SELECT") && parse_select(p, st); }
    if (accept_kw(p, "CREATE")) {
        st->kind = ST_CREATE_INDEX; st->ix_kind = -1;
        if (!expect_kw(p, "INDEX")) return 0;
        if (peek(p)->k == TK_ID && !is_kw(peek(p), "ON")) { if (!ident(p, st->name, sizeof st->name)) return 0; }
        if (!expect_kw(p, "ON") || !ident(p, st->table, sizeof st->table) || !expect_sym(p, "(") || !ident(p, st->col, sizeof st->col)) return 0;
        if (accept_sym(p, ",") && !ident(p, st->col2, sizeof st->col2)) return 0;
        if (!expect_sym(p, ")")) return 0;
        if (accept_kw(p, "USING")) {
            char k[16]; if (!ident(p, k, sizeof k)) return 0;
            if (!strcasecmp(k, "BTREE")) st->ix_kind = IX_BTREE; else if (!strcasecmp(k, "HASH")) st->ix_kind = IX_HASH;
            else if (!strcasecmp(k, "TRIE")) st->ix_kind = IX_TRIE; else if (!strcasecmp(k, "INTERVAL")) st->ix_kind = IX_INTERVAL;
            else FAIL("unknown index type '%s' (use BTREE, HASH, TRIE or INTERVAL)", k);
        }
        return 1;
    }
    if (accept_kw(p, "DROP")) { st->kind = ST_DROP_INDEX; return expect_kw(p, "INDEX") && ident(p, st->name, sizeof st->name); }
    if (accept_kw(p, "LOAD")) {
        st->kind = ST_LOAD;
        if (!ident(p, st->table, sizeof st->table) || !expect_kw(p, "FROM")) return 0;
        Tok *s = take(p);
        if (s->k != TK_STR) FAIL("syntax error: LOAD ... FROM 'file.csv'");
        snprintf(st->path, sizeof st->path, "%s", s->s); return 1;
    }
    if (accept_kw(p, "SHOW")) {
        if (accept_kw(p, "TABLES")) { st->kind = ST_SHOW_TABLES; return 1; }
        if (accept_kw(p, "INDEXES") || accept_kw(p, "INDICES")) { st->kind = ST_SHOW_INDEXES; return 1; }
        FAIL("syntax error: SHOW TABLES | SHOW INDEXES");
    }
    if (accept_kw(p, "DESCRIBE") || accept_kw(p, "DESC")) { st->kind = ST_DESCRIBE; return ident(p, st->table, sizeof st->table); }
    if (accept_kw(p, "INSERT")) {
        st->kind = ST_INSERT;
        if (!expect_kw(p, "INTO") || !ident(p, st->table, sizeof st->table) || !expect_kw(p, "VALUES") || !expect_sym(p, "(")) return 0;
        do {
            if (st->nvals >= MAX_VALS) FAIL("too many values");
            if (!literal(p, &st->vals[st->nvals++])) return 0;
        } while (accept_sym(p, ","));
        return expect_sym(p, ")");
    }
    if (accept_kw(p, "HELP")) { st->kind = ST_HELP; return 1; }
    if (accept_kw(p, "QUIT") || accept_kw(p, "EXIT")) { st->kind = ST_QUIT; return 1; }
    FAIL("syntax error: unknown statement '%s' (try HELP)", t->s);
}

int sql_parse(const char *text, Stmt *out, char *err, size_t en) {
    static P p;
    memset(out, 0, sizeof *out);
    p.err = err; p.en = en; err[0] = '\0';
    if (!lex(text, &p)) return 0;
    if (!parse_stmt(&p, out)) return 0;
    accept_sym(&p, ";");
    if (peek(&p)->k != TK_END) { snprintf(err, en, "syntax error: unexpected '%s'", peek(&p)->s); return 0; }
    return 1;
}
