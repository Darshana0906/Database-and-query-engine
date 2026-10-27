/* bptree.h - in-memory B+ tree over (int64 key, int64 rid); duplicate keys allowed. */
#ifndef GATEDB_BPTREE_H
#define GATEDB_BPTREE_H
#include "common.h"
typedef struct BPTree BPTree;
BPTree *bpt_new(void);
void    bpt_free(BPTree *t);
void    bpt_insert(BPTree *t, int64_t key, int64_t rid);
void    bpt_range(const BPTree *t, int64_t lo, int64_t hi, RidVec *out); /* inclusive, key order */
size_t  bpt_count(const BPTree *t);
int     bpt_height(const BPTree *t);
size_t  bpt_nodes(const BPTree *t);
int     bpt_check(const BPTree *t);   /* validates all invariants; 1 = ok */
#endif
