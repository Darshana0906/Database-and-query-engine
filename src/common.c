#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <ctype.h>

void *xmalloc(size_t n) { void *p = malloc(n ? n : 1); if (!p) { fputs("out of memory\n", stderr); exit(2); } return p; }
void *xcalloc(size_t n, size_t sz) { void *p = calloc(n ? n : 1, sz ? sz : 1); if (!p) { fputs("out of memory\n", stderr); exit(2); } return p; }
void *xrealloc(void *p, size_t n) { p = realloc(p, n ? n : 1); if (!p) { fputs("out of memory\n", stderr); exit(2); } return p; }

char *astrdup(const char *s) {
    static char *chunk; static size_t left;
    size_t n = strlen(s) + 1;
    if (n > 4096) { char *p = xmalloc(n); memcpy(p, s, n); return p; }
    if (n > left) { chunk = xmalloc(1u << 20); left = 1u << 20; }
    char *p = chunk; memcpy(p, s, n); chunk += n; left -= n;
    return p;
}

double now_ms(void) {
    struct timespec ts; clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1e6;
}

uint64_t fnv1a(const char *s) {
    uint64_t h = 1469598103934665603ULL;
    for (; *s; s++) { h ^= (unsigned char)*s; h *= 1099511628211ULL; }
    return h;
}

void rv_init(RidVec *v) { v->a = NULL; v->n = v->cap = 0; }
void rv_push(RidVec *v, int64_t x) {
    if (v->n == v->cap) { v->cap = v->cap ? v->cap * 2 : 4; v->a = xrealloc(v->a, v->cap * sizeof *v->a); }
    v->a[v->n++] = x;
}
void rv_free(RidVec *v) { free(v->a); rv_init(v); }

int value_cmp(const Value *a, const Value *b) {
    if (a->null || b->null) return (int)b->null - (int)a->null; /* both null: 0; null < non-null */
    if (a->type == T_TEXT) { int c = strcmp(a->s, b->s); return c < 0 ? -1 : c > 0; }
    return a->i < b->i ? -1 : a->i > b->i;
}

/* ---- timestamps: civil-date arithmetic, no locale or TZ dependence ---- */
static int64_t days_from_civil(int y, int m, int d) {
    y -= m <= 2;
    int64_t era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (unsigned)((153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1);
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + (int64_t)doe - 719468;
}

static int rd_int(const char **p, int *out) {
    char *e; long v = strtol(*p, &e, 10);
    if (e == *p) return 0;
    *out = (int)v; *p = e; return 1;
}

int parse_timestamp(const char *s, int64_t *out) {
    int Y, M, D, h = 0, mi = 0, sec = 0;
    const char *p = s;
    while (*p == ' ') p++;
    if (!rd_int(&p, &Y) || *p++ != '-' || !rd_int(&p, &M) || *p++ != '-' || !rd_int(&p, &D)) return 0;
    if (M < 1 || M > 12 || D < 1 || D > 31) return 0;
    int64_t off = 0;
    if (*p == ' ' || *p == 'T') {
        p++;
        if (!rd_int(&p, &h) || *p++ != ':' || !rd_int(&p, &mi)) return 0;
        if (*p == ':') { p++; if (!rd_int(&p, &sec)) return 0; }
        if (*p == '.') { p++; while (isdigit((unsigned char)*p)) p++; }
        if (*p == 'Z') p++;
        else if (*p == '+' || *p == '-') {
            int sgn = *p == '-' ? -1 : 1, oh = 0, om = 0; p++;
            if (!rd_int(&p, &oh)) return 0;
            if (*p == ':') { p++; if (!rd_int(&p, &om)) return 0; }
            off = sgn * (oh * 3600 + om * 60);
        }
        if (h < 0 || h > 23 || mi < 0 || mi > 59 || sec < 0 || sec > 60) return 0;
    }
    while (*p == ' ') p++;
    if (*p) return 0;
    *out = days_from_civil(Y, M, D) * 86400 + h * 3600 + mi * 60 + sec - off;
    return 1;
}

void format_timestamp(int64_t t, char *buf, size_t n) {
    time_t tt = (time_t)t; struct tm tmv;
    gmtime_r(&tt, &tmv);
    snprintf(buf, n, "%04d-%02d-%02d %02d:%02d:%02d", tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
}

void value_str(const Value *v, char *buf, size_t n) {
    if (v->null) { buf[0] = '\0'; return; }
    if (v->type == T_INT) snprintf(buf, n, "%lld", (long long)v->i);
    else if (v->type == T_TS) format_timestamp(v->i, buf, n);
    else snprintf(buf, n, "%s", v->s);
}

const char *value_key(const Value *v, char *buf, size_t n) {
    if (v->type == T_TEXT) return v->s;
    snprintf(buf, n, "%lld", (long long)v->i);
    return buf;
}

int like_match(const char *p, const char *s) {
    const char *star = NULL, *ss = s;
    while (*s) {
        if (*p == '%') { star = p++; ss = s; }
        else if (*p == '_' || tolower((unsigned char)*p) == tolower((unsigned char)*s)) { p++; s++; }
        else if (star) { p = star + 1; s = ++ss; }
        else return 0;
    }
    while (*p == '%') p++;
    return *p == '\0';
}
