#include "hashidx.h"
#include <stdlib.h>
#include <string.h>

HashIdx *hx_new(void) {
    HashIdx *h = xcalloc(1, sizeof *h);
    h->nb = 64; h->b = xcalloc(h->nb, sizeof *h->b);
    return h;
}

void hx_free(HashIdx *h) {
    if (!h) return;
    for (size_t i = 0; i < h->nb; i++)
        for (HNode *n = h->b[i], *nx; n; n = nx) { nx = n->next; free(n->key); rv_free(&n->rids); free(n); }
    free(h->b); free(h);
}

static void grow(HashIdx *h) {
    size_t nb = h->nb * 2;
    HNode **b = xcalloc(nb, sizeof *b);
    for (size_t i = 0; i < h->nb; i++)
        for (HNode *n = h->b[i], *nx; n; n = nx) {
            nx = n->next;
            size_t k = fnv1a(n->key) % nb;
            n->next = b[k]; b[k] = n;
        }
    free(h->b); h->b = b; h->nb = nb;
}

static HNode *find_node(const HashIdx *h, const char *key) {
    for (HNode *n = h->b[fnv1a(key) % h->nb]; n; n = n->next)
        if (strcmp(n->key, key) == 0) return n;
    return NULL;
}

void hx_insert(HashIdx *h, const char *key, int64_t rid) {
    HNode *n = find_node(h, key);
    if (!n) {
        if (h->nkeys * 4 > h->nb * 3) grow(h);          /* keep load factor <= 0.75 */
        n = xcalloc(1, sizeof *n);
        size_t len = strlen(key) + 1;
        n->key = xmalloc(len); memcpy(n->key, key, len);
        size_t k = fnv1a(key) % h->nb;
        n->next = h->b[k]; h->b[k] = n; h->nkeys++;
    }
    rv_push(&n->rids, rid); h->nentries++;
}

const RidVec *hx_find(const HashIdx *h, const char *key) {
    HNode *n = find_node(h, key);
    return n ? &n->rids : NULL;
}

size_t hx_collect(const HashIdx *h, HNode ***out) {
    HNode **a = xmalloc((h->nkeys ? h->nkeys : 1) * sizeof *a);
    size_t m = 0;
    for (size_t i = 0; i < h->nb; i++) for (HNode *n = h->b[i]; n; n = n->next) a[m++] = n;
    *out = a; return m;
}

size_t hx_max_chain(const HashIdx *h) {
    size_t best = 0;
    for (size_t i = 0; i < h->nb; i++) { size_t c = 0; for (HNode *n = h->b[i]; n; n = n->next) c++; if (c > best) best = c; }
    return best;
}
