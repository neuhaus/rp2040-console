#ifndef NETHACK_POOL_H
#define NETHACK_POOL_H
#include <stddef.h>
#include <sys/reent.h>
void *nh_pool_malloc(struct _reent *, size_t);
void *nh_pool_calloc(struct _reent *, size_t, size_t);
void *nh_pool_realloc(struct _reent *, void *, size_t);
void nh_pool_free(struct _reent *, void *);
#endif
