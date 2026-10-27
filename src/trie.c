#include "trie.h"
#include <stdlib.h>
#include <ctype.h>

typedef struct TNode { unsigned char c; struct TNode *child, *sib; RidVec *rids; } TNode;
struct Trie { TNode *root; size_t nodes; };

static TNode *tn_new(Trie *t, unsigned char c) { TNode *n = xcalloc(1, sizeof *n); n->c = c; t->nodes++; return n; }

Trie *trie_new(void) { Trie *t = xcalloc(1, sizeof *t); t->root = tn_new(t, 0); return t; }

static void tn_free(TNode *n) {
    while (n) {
        TNode *nx = n->sib;
        tn_free(n->child);
        if (n->rids) { rv_free(n->rids); free(n->rids); }
        free(n); n = nx;
    }
}
void trie_free(Trie *t) { if (t) { tn_free(t->root); free(t); } }

void trie_insert(Trie *t, const char *key, int64_t rid) {
    TNode *n = t->root;
    for (; *key; key++) {
        unsigned char c = (unsigned char)tolower((unsigned char)*key);
        TNode *ch = n->child;
        while (ch && ch->c != c) ch = ch->sib;
        if (!ch) { ch = tn_new(t, c); ch->sib = n->child; n->child = ch; }
        n = ch;
    }
    if (!n->rids) { n->rids = xmalloc(sizeof *n->rids); rv_init(n->rids); }
    rv_push(n->rids, rid);
}

static void collect(const TNode *n, RidVec *out) {
    if (n->rids) for (size_t i = 0; i < n->rids->n; i++) rv_push(out, n->rids->a[i]);
    for (const TNode *c = n->child; c; c = c->sib) collect(c, out);
}

void trie_prefix(const Trie *t, const char *prefix, RidVec *out) {
    const TNode *n = t->root;
    for (; *prefix; prefix++) {
        unsigned char c = (unsigned char)tolower((unsigned char)*prefix);
        const TNode *ch = n->child;
        while (ch && ch->c != c) ch = ch->sib;
        if (!ch) return;
        n = ch;
    }
    collect(n, out);
}

size_t trie_nodes(const Trie *t) { return t->nodes; }
