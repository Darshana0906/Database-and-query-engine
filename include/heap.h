/* heap.h - binary heap of pointers. Root is the element x for which cmp(x, y) >= 0 for all y (a max-heap under cmp). */
#ifndef GATEDB_HEAP_H
#define GATEDB_HEAP_H
#include <stddef.h>
typedef int (*HeapCmp)(const void *a, const void *b, void *ctx);
typedef struct { void **a; size_t n, cap; HeapCmp cmp; void *ctx; } Heap;
void  heap_init(Heap *h, HeapCmp cmp, void *ctx);
void  heap_free(Heap *h);
void  heap_push(Heap *h, void *x);
void *heap_top(const Heap *h);
void *heap_pop(Heap *h);
#endif
