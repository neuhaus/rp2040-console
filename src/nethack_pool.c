#include "nethack_pool.h"
#include <errno.h>
#include <stdint.h>
#include <string.h>
extern void *__real__malloc_r(struct _reent *, size_t);
extern void *__real__realloc_r(struct _reent *, void *, size_t);
extern void __real__free_r(struct _reent *, void *);
#include "nethack_pooled64.h"
extern void nh_pool_note_alloc(size_t, void *, void *) __attribute__((weak));
static void note(size_t n, void *p) {
    if (nh_pool_note_alloc) nh_pool_note_alloc(n, p, __builtin_return_address(0));
}
void *nh_pool_malloc(struct _reent *r, size_t n) { void *p = nh64_malloc(r, n); note(n, p); return p; }
void nh_pool_free(struct _reent *r, void *p) { nh64_free(r, p); }
void *nh_pool_realloc(struct _reent *r, void *p, size_t n) { void *q = nh64_realloc(r, p, n); note(n, q); return q; }
void *nh_pool_calloc(struct _reent *r, size_t n, size_t s) {
    if (s && n > SIZE_MAX / s) { _REENT_ERRNO(r) = ENOMEM; return NULL; }
    void *p = nh64_malloc(r, n * s);
    if (p) memset(p, 0, n * s);
    note(n * s, p);
    return p;
}
#ifndef RP2040_CONSOLE_SOFT_REBOOT
void *__wrap__malloc_r(struct _reent *r, size_t n) { return nh_pool_malloc(r, n); }
void *__wrap__calloc_r(struct _reent *r, size_t n, size_t s) { return nh_pool_calloc(r, n, s); }
void *__wrap__realloc_r(struct _reent *r, void *p, size_t n) { return nh_pool_realloc(r, p, n); }
void __wrap__free_r(struct _reent *r, void *p) { nh_pool_free(r, p); }
#endif
