/* table.h - row storage, schemas, index management and the CSV loader. */
#ifndef GATEDB_TABLE_H
#define GATEDB_TABLE_H
#include "common.h"
#include "bptree.h"
#include "hashidx.h"
#include "trie.h"
#include "itree.h"

#define MAX_IDX  16
#define MAX_TABLES 32

typedef struct {
    char name[80];
    IndexKind kind;
    int col, col2;                 /* col2 only for INTERVAL (the end column) */
    BPTree *bt; HashIdx *hx; Trie *tr; ITree *it;
    double build_ms; int64_t entries;
} Index;

typedef struct {
    char name[32];
    int ncols; Column cols[MAX_COLS];
    Value *data;                   /* row-major: row i starts at data + i*ncols */
    int64_t nrows, cap;
    Index *idx[MAX_IDX]; int nidx;
} Table;

typedef struct { Table *t[MAX_TABLES]; int n; } Catalog;

void    catalog_init(Catalog *c);                  /* creates the six hostel tables (empty) */
Table  *catalog_create_table(Catalog *c, const char *name, const Column *cols, int ncols, char *err, size_t en);
Table  *catalog_find(const Catalog *c, const char *name);
int     table_col(const Table *t, const char *name);   /* -1 if unknown */
const char *coltype_name(ColType t);
const char *indexkind_name(IndexKind k);

void    table_append(Table *t, const Value *row);      /* copies the row and maintains all indexes */
int     table_load_csv(Table *t, const char *path, int64_t *loaded, char *err, size_t en);

Index  *table_find_index(const Table *t, IndexKind k, int col, int col2);
Index  *table_create_index(Table *t, const char *name, IndexKind k, int col, int col2, char *err, size_t en);
int     table_drop_index(Table *t, const char *name);
void    index_describe(const Index *ix, const Table *t, char *buf, size_t n);
#endif
