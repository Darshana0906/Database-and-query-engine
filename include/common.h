/* common.h - shared types and small utilities (C11, standard library only) */
#ifndef GATEDB_COMMON_H
#define GATEDB_COMMON_H
#include <stdint.h>
#include <stddef.h>

typedef enum { T_INT = 0, T_TEXT = 1, T_TS = 2 } ColType;
typedef enum { IX_BTREE = 0, IX_HASH = 1, IX_TRIE = 2, IX_INTERVAL = 3 } IndexKind;

#define MAX_COLS 16
typedef struct { char name[32]; ColType type; } Column;

/* One cell. TEXT strings are arena-owned (never freed individually). */
typedef struct {
    int64_t     i;     /* INT value, or TS as Unix seconds (UTC) */
    const char *s;     /* TEXT value */
    uint8_t     type;
    uint8_t     null;
} Value;

/* Growable vector of row ids. */
typedef struct { int64_t *a; size_t n, cap; } RidVec;
void rv_init(RidVec *v);
void rv_push(RidVec *v, int64_t x);
void rv_free(RidVec *v);

void    *xmalloc(size_t n);
void    *xcalloc(size_t n, size_t sz);
void    *xrealloc(void *p, size_t n);
char    *astrdup(const char *s);       /* arena-backed strdup */
double   now_ms(void);
uint64_t fnv1a(const char *s);

int      value_cmp(const Value *a, const Value *b);   /* NULLs sort first */
void     value_str(const Value *v, char *buf, size_t n);
const char *value_key(const Value *v, char *buf, size_t n); /* hash-key text */

int      parse_timestamp(const char *s, int64_t *out); /* 1 = ok. Accepts YYYY-MM-DD[ T]HH:MM[:SS[.f]][Z|+hh[:mm]] */
void     format_timestamp(int64_t t, char *buf, size_t n);
int      like_match(const char *pat, const char *s);   /* case-insensitive, % and _ */
#endif
