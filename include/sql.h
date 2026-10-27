/* sql.h - tokenizer + recursive-descent parser for a small SQL subset. */
#ifndef GATEDB_SQL_H
#define GATEDB_SQL_H
#include "common.h"

#define MAX_SEL 16
#define MAX_PRED 8
#define MAX_VALS 16

typedef struct { int is_str, is_null; int64_t n; char s[160]; } Lit;
typedef struct { char tbl[32]; char col[32]; } ColRef;

typedef enum { SEL_STAR, SEL_COL, SEL_COUNT } SelKind;
typedef struct { SelKind kind; ColRef ref; } SelItem;

typedef enum { OP_EQ, OP_NE, OP_LT, OP_LE, OP_GT, OP_GE, OP_LIKE, OP_ISNULL, OP_NOTNULL, OP_OVERLAP } PredOp;
typedef struct { PredOp op; ColRef ref, ref2; Lit lit, lit2; } Pred;

typedef enum {
    ST_EMPTY, ST_SELECT, ST_EXPLAIN, ST_CREATE_INDEX, ST_DROP_INDEX, ST_LOAD,
    ST_SHOW_TABLES, ST_SHOW_INDEXES, ST_DESCRIBE, ST_INSERT, ST_HELP, ST_QUIT
} StmtKind;

typedef struct {
    StmtKind kind;
    /* SELECT / EXPLAIN */
    SelItem sel[MAX_SEL]; int nsel;
    char from[32], from_alias[32];
    int has_join; char join[32], join_alias[32]; ColRef on_a, on_b;
    Pred preds[MAX_PRED]; int npreds;
    int has_group; ColRef group;
    int has_order, order_count, order_desc; ColRef order;
    int64_t limit;                               /* -1 = none */
    /* DDL / DML */
    char name[48], table[32], col[32], col2[32];
    int ix_kind;                                 /* -1 = choose by column type, else IndexKind */
    char path[256];
    Lit vals[MAX_VALS]; int nvals;
} Stmt;

int sql_parse(const char *text, Stmt *out, char *err, size_t en);   /* 1 = ok */
#endif
