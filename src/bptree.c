#include "bptree.h"
#include <stdlib.h>
#include <string.h>

#define BP_MAX 32  /* max keys per node; a node splits when it would hold BP_MAX+1 */

/* Entries are the composite (key, rid), so duplicate keys are totally ordered. */
typedef struct { int64_t k, r; } Ent;
typedef struct BPNode {
    int leaf, n;
    Ent e[BP_MAX + 1];
    struct BPNode *c[BP_MAX + 2];   /* internal nodes only */
    struct BPNode *next;            /* leaf chain */
} BPNode;
struct BPTree { BPNode *root; size_t count, nodes; };

static int ecmp(Ent a, Ent b) {
    if (a.k != b.k) return a.k < b.k ? -1 : 1;
    if (a.r != b.r) return a.r < b.r ? -1 : 1;
    return 0;
}

static BPNode *node_new(BPTree *t, int leaf) {
    BPNode *x = xcalloc(1, sizeof *x); x->leaf = leaf; t->nodes++; return x;
}

BPTree *bpt_new(void) {
    BPTree *t = xcalloc(1, sizeof *t);
    t->root = node_new(t, 1);
    return t;
}

static void free_node(BPNode *x) {
    if (!x->leaf) for (int i = 0; i <= x->n; i++) free_node(x->c[i]);
    free(x);
}
void bpt_free(BPTree *t) { if (t) { free_node(t->root); free(t); } }

/* Recursive insert. Returns 1 if x split; then *up is the separator and *right the new sibling. */
static int ins(BPTree *t, BPNode *x, Ent en, Ent *up, BPNode **right) {
    if (x->leaf) {
        int i = x->n;
        while (i > 0 && ecmp(x->e[i - 1], en) > 0) { x->e[i] = x->e[i - 1]; i--; }
        x->e[i] = en; x->n++;
        if (x->n <= BP_MAX) return 0;
        int mid = x->n / 2;
        BPNode *r = node_new(t, 1);
        r->n = x->n - mid;
        memcpy(r->e, x->e + mid, (size_t)r->n * sizeof(Ent));
        x->n = mid;
        r->next = x->next; x->next = r;
        *up = r->e[0]; *right = r;       /* B+ tree: separator is copied up, stays in the leaf */
        return 1;
    }
    int i = 0;
    while (i < x->n && ecmp(en, x->e[i]) >= 0) i++;
    Ent cup; BPNode *cr;
    if (!ins(t, x->c[i], en, &cup, &cr)) return 0;
    for (int j = x->n; j > i; j--) x->e[j] = x->e[j - 1];
    for (int j = x->n + 1; j > i + 1; j--) x->c[j] = x->c[j - 1];
    x->e[i] = cup; x->c[i + 1] = cr; x->n++;
    if (x->n <= BP_MAX) return 0;
    int mid = x->n / 2;
    BPNode *r = node_new(t, 0);
    *up = x->e[mid];                     /* internal split: separator moves up */
    r->n = x->n - mid - 1;
    memcpy(r->e, x->e + mid + 1, (size_t)r->n * sizeof(Ent));
    memcpy(r->c, x->c + mid + 1, (size_t)(r->n + 1) * sizeof(BPNode *));
    x->n = mid;
    *right = r;
    return 1;
}

void bpt_insert(BPTree *t, int64_t key, int64_t rid) {
    Ent en = { key, rid }, up; BPNode *r;
    if (ins(t, t->root, en, &up, &r)) {
        BPNode *nr = node_new(t, 0);
        nr->n = 1; nr->e[0] = up; nr->c[0] = t->root; nr->c[1] = r;
        t->root = nr;
    }
    t->count++;
}

void bpt_range(const BPTree *t, int64_t lo, int64_t hi, RidVec *out) {
    if (lo > hi) return;
    Ent tg = { lo, INT64_MIN };
    const BPNode *x = t->root;
    while (!x->leaf) {
        int i = 0;
        while (i < x->n && ecmp(tg, x->e[i]) >= 0) i++;
        x = x->c[i];
    }
    int i = 0;
    while (i < x->n && ecmp(x->e[i], tg) < 0) i++;
    while (x) {
        for (; i < x->n; i++) {
            if (x->e[i].k > hi) return;
            rv_push(out, x->e[i].r);
        }
        x = x->next; i = 0;
    }
}

size_t bpt_count(const BPTree *t) { return t->count; }
size_t bpt_nodes(const BPTree *t) { return t->nodes; }
int bpt_height(const BPTree *t) {
    int h = 1; for (const BPNode *x = t->root; !x->leaf; x = x->c[0]) h++;
    return h;
}

/* ---- invariant checker (used by the test-suite) ---- */
static int chk(const BPNode *x, int depth, int *leafdepth, size_t *cnt, const Ent *lo, const Ent *hi) {
    for (int i = 0; i < x->n; i++) {
        if (lo && ecmp(x->e[i], *lo) < 0) return 0;
        if (hi && ecmp(x->e[i], *hi) >= 0) return 0;
        if (i && ecmp(x->e[i - 1], x->e[i]) >= 0) return 0;
    }
    if (x->leaf) {
        if (*leafdepth < 0) *leafdepth = depth;
        if (*leafdepth != depth) return 0;          /* all leaves at same depth */
        *cnt += (size_t)x->n;
        return 1;
    }
    if (x->n < 1) return 0;
    for (int i = 0; i <= x->n; i++) {
        const Ent *l = i ? &x->e[i - 1] : lo, *h = i < x->n ? &x->e[i] : hi;
        if (!chk(x->c[i], depth + 1, leafdepth, cnt, l, h)) return 0;
    }
    return 1;
}

int bpt_check(const BPTree *t) {
    int ld = -1; size_t cnt = 0;
    if (!chk(t->root, 0, &ld, &cnt, NULL, NULL) || cnt != t->count) return 0;
    const BPNode *x = t->root;
    while (!x->leaf) x = x->c[0];
    size_t seen = 0; int have = 0; Ent prev = { 0, 0 };
    for (; x; x = x->next)
        for (int i = 0; i < x->n; i++) {
            if (have && ecmp(prev, x->e[i]) >= 0) return 0;   /* leaf chain strictly sorted */
            prev = x->e[i]; have = 1; seen++;
        }
    return seen == t->count;
}
