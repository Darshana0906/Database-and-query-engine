/* trie.h - case-insensitive prefix tree (first-child / next-sibling) mapping strings to row ids. */
#ifndef GATEDB_TRIE_H
#define GATEDB_TRIE_H
#include "common.h"
typedef struct Trie Trie;
Trie  *trie_new(void);
void   trie_free(Trie *t);
void   trie_insert(Trie *t, const char *key, int64_t rid);
void   trie_prefix(const Trie *t, const char *prefix, RidVec *out);  /* every rid whose key starts with prefix */
size_t trie_nodes(const Trie *t);
#endif
