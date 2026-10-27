#include "itree.h"
#include <stdlib.h>

struct INode { int64_t lo, hi, maxhi, rid; uint32_t pri; struct INode *l, *r; };

ITree *it_new(void) { ITree *t = xcalloc(1, sizeof *t); t->seed = 2463534242u; return t; }

static void free_n(INode *n) { if (n) { free_n(n->l); free_n(n->r); free(n); } }
void it_free(ITree *t) { if (t) { free_n(t->root); free(t); } }

static uint32_t rnd(ITree *t) { uint32_t x = t->seed; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return t->seed = x; }

static void pull(INode *x) {
    x->maxhi = x->hi;
    if (x->l && x->l->maxhi > x->maxhi) x->maxhi = x->l->maxhi;
    if (x->r && x->r->maxhi > x->maxhi) x->maxhi = x->r->maxhi;
}
static INode *rot_right(INode *y) { INode *x = y->l; y->l = x->r; x->r = y; pull(y); pull(x); return x; }
static INode *rot_left(INode *x)  { INode *y = x->r; x->r = y->l; y->l = x; pull(x); pull(y); return y; }

static INode *ins(INode *n, INode *nw) {
    if (!n) return nw;
    if (nw->lo < n->lo) { n->l = ins(n->l, nw); if (n->l->pri > n->pri) n = rot_right(n); }
    else                { n->r = ins(n->r, nw); if (n->r->pri > n->pri) n = rot_left(n); }
    pull(n);
    return n;
}

void it_insert(ITree *t, int64_t lo, int64_t hi, int64_t rid) {
    INode *nw = xcalloc(1, sizeof *nw);
    nw->lo = lo; nw->hi = hi; nw->maxhi = hi; nw->rid = rid; nw->pri = rnd(t);
    t->root = ins(t->root, nw); t->n++;
}

static void q(const INode *n, int64_t lo, int64_t hi, RidVec *out) {
    if (!n) return;
    if (n->l && n->l->maxhi >= lo) q(n->l, lo, hi, out);   /* prune: nothing in left reaches lo */
    if (n->lo <= hi && n->hi >= lo) rv_push(out, n->rid);
    if (n->lo <= hi) q(n->r, lo, hi, out);                 /* prune: right starts after hi */
}
void it_overlap(const ITree *t, int64_t qlo, int64_t qhi, RidVec *out) { if (qlo <= qhi) q(t->root, qlo, qhi, out); }

static int height(const INode *n) { if (!n) return 0; int a = height(n->l), b = height(n->r); return 1 + (a > b ? a : b); }
int it_height(const ITree *t) { return height(t->root); }

static int chk(const INode *n, int64_t *mn, int64_t *mx, int64_t *maxhi) {
    if (!n) return 1;
    int64_t lmn = 0, lmx = 0, lmh = INT64_MIN, rmn = 0, rmx = 0, rmh = INT64_MIN;
    if (n->l) { if (!chk(n->l, &lmn, &lmx, &lmh) || lmx > n->lo || n->l->pri > n->pri) return 0; }
    if (n->r) { if (!chk(n->r, &rmn, &rmx, &rmh) || rmn < n->lo || n->r->pri > n->pri) return 0; }
    int64_t m = n->hi; if (lmh > m) m = lmh; if (rmh > m) m = rmh;
    if (m != n->maxhi) return 0;
    *mn = n->l ? lmn : n->lo; *mx = n->r ? rmx : n->lo; *maxhi = m;
    return 1;
}
int it_check(const ITree *t) { int64_t a, b, c; return chk(t->root, &a, &b, &c); }
