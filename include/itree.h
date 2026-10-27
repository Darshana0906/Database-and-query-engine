/* itree.h - interval tree: a treap keyed by interval start, augmented with the max end of each subtree. */
#ifndef GATEDB_ITREE_H
#define GATEDB_ITREE_H
#include "common.h"
typedef struct INode INode;
typedef struct { INode *root; size_t n; uint32_t seed; } ITree;
ITree *it_new(void);
void   it_free(ITree *t);
void   it_insert(ITree *t, int64_t lo, int64_t hi, int64_t rid);       /* closed interval [lo, hi] */
void   it_overlap(const ITree *t, int64_t qlo, int64_t qhi, RidVec *out); /* all intervals intersecting [qlo, qhi] */
int    it_height(const ITree *t);
int    it_check(const ITree *t);   /* BST order, heap priorities, max-end augmentation */
#endif
