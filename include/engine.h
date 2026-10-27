/* engine.h - planner + executor. Runs parsed statements against a Catalog. */
#ifndef GATEDB_ENGINE_H
#define GATEDB_ENGINE_H
#include <stdio.h>
#include "table.h"
#include "sql.h"

typedef struct { int use_indexes; int use_topk_heap; } EngineOpts;
extern EngineOpts engine_opts;        /* switches used by the benchmark to compare strategies */

typedef struct {
    int64_t  rows;                    /* rows returned */
    int64_t  scanned;                 /* rows examined (candidates fetched) */
    double   ms;                      /* execution time, excluding printing */
    int      want_checksum;           /* in:  compute an order-independent checksum of the result */
    uint64_t checksum;                /* out */
    char     plan[512];               /* out: one-line plan summary */
} ExecInfo;

/* Returns 1 on success. Output (if any) goes to `out`; result rows are printed only if print_rows. */
int  engine_exec(Catalog *cat, const Stmt *st, FILE *out, int print_rows, ExecInfo *info, char *err, size_t en);
void engine_help(FILE *out);
#endif
