#include "heap.h"
#include "common.h"
#include <stdlib.h>

void heap_init(Heap *h, HeapCmp cmp, void *ctx) { h->a = NULL; h->n = h->cap = 0; h->cmp = cmp; h->ctx = ctx; }
void heap_free(Heap *h) { free(h->a); h->a = NULL; h->n = h->cap = 0; }

void heap_push(Heap *h, void *x) {
    if (h->n == h->cap) { h->cap = h->cap ? h->cap * 2 : 16; h->a = xrealloc(h->a, h->cap * sizeof *h->a); }
    size_t i = h->n++;
    while (i > 0) {                       /* sift up */
        size_t p = (i - 1) / 2;
        if (h->cmp(h->a[p], x, h->ctx) >= 0) break;
        h->a[i] = h->a[p]; i = p;
    }
    h->a[i] = x;
}

void *heap_top(const Heap *h) { return h->n ? h->a[0] : NULL; }

void *heap_pop(Heap *h) {
    if (!h->n) return NULL;
    void *top = h->a[0], *x = h->a[--h->n];
    size_t i = 0;
    for (;;) {                            /* sift down */
        size_t l = 2 * i + 1, r = l + 1, m = l;
        if (l >= h->n) break;
        if (r < h->n && h->cmp(h->a[r], h->a[l], h->ctx) > 0) m = r;
        if (h->cmp(x, h->a[m], h->ctx) >= 0) break;
        h->a[i] = h->a[m]; i = m;
    }
    if (h->n) h->a[i] = x;
    return top;
}
