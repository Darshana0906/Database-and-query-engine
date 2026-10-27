/* hashidx.h - chained hash index: string key -> list of row ids. Grows by rehashing. */
#ifndef GATEDB_HASHIDX_H
#define GATEDB_HASHIDX_H
#include "common.h"
typedef struct HNode { char *key; RidVec rids; struct HNode *next; } HNode;
typedef struct { HNode **b; size_t nb, nkeys, nentries; } HashIdx;
HashIdx      *hx_new(void);
void          hx_free(HashIdx *h);
void          hx_insert(HashIdx *h, const char *key, int64_t rid);
const RidVec *hx_find(const HashIdx *h, const char *key);   /* NULL if absent */
size_t        hx_collect(const HashIdx *h, HNode ***out);   /* all key nodes; caller frees *out */
size_t        hx_max_chain(const HashIdx *h);
#endif
