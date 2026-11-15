/* newlib allocator attribution for the session core.
 *
 * Every newlib allocation belongs to a reentrancy context, so comparing the
 * context against the program's own separates program objects and libc
 * buffers from the platform's. The ledger of program pointers is what the
 * session sweep reclaims, including objects the program leaked.
 *
 * The ledger nodes themselves are platform state: they are allocated with the
 * real allocator and _GLOBAL_REENT so the sweep's own bookkeeping is never
 * attributed to the program. */
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include "rp2040-soft-reboot.h"
#include "soft_reboot_internal.h"
#include "soft_reboot_newlib.h"

struct allocation {
    void *pointer;
    struct allocation *next;
};

static struct allocation *allocations;
static unsigned allocator_depth;

extern void *__real__malloc_r(struct _reent *, size_t);
extern void *__real__calloc_r(struct _reent *, size_t, size_t);
extern void *__real__realloc_r(struct _reent *, void *, size_t);
extern void __real__free_r(struct _reent *, void *);

#ifdef RP2040_NETHACK_POOL64
#include "nethack_pool.h"
#define program_malloc nh_pool_malloc
#define program_calloc nh_pool_calloc
#define program_realloc nh_pool_realloc
#define program_free nh_pool_free
#else
#define program_malloc __real__malloc_r
#define program_calloc __real__calloc_r
#define program_realloc __real__realloc_r
#define program_free __real__free_r
#endif

static struct allocation **find(void *pointer)
{
    struct allocation **link = &allocations;
    while (*link && (*link)->pointer != pointer) link = &(*link)->next;
    return link;
}

static void forget(void *pointer)
{
    if (pointer == NULL) return;
    struct allocation **link = find(pointer);
    if (*link) {
        struct allocation *entry = *link;
        *link = entry->next;
        __real__free_r(_GLOBAL_REENT, entry);
    }
}

static void *remember(struct _reent *context, void *pointer)
{
    if (pointer == NULL || !rp2040_soft_active() || context != &rp2040_program_reent)
        return pointer;
    struct allocation *entry = __real__malloc_r(_GLOBAL_REENT, sizeof(*entry));
    if (entry == NULL) {
        program_free(context, pointer);
        _REENT_ERRNO(context) = ENOMEM;
        return NULL;
    }
    entry->pointer = pointer;
    entry->next = allocations;
    allocations = entry;
    return pointer;
}

void *__wrap__malloc_r(struct _reent *context, size_t size)
{
    int outer = allocator_depth++ == 0;
    void *pointer = program_malloc(context, size);
    if (outer) pointer = remember(context, pointer);
    --allocator_depth;
    return pointer;
}

void *__wrap__calloc_r(struct _reent *context, size_t count, size_t size)
{
    if (size && count > SIZE_MAX / size) { _REENT_ERRNO(context) = ENOMEM; return NULL; }
    int outer = allocator_depth++ == 0;
    void *pointer = program_calloc(context, count, size);
    if (outer) pointer = remember(context, pointer);
    --allocator_depth;
    return pointer;
}

void __wrap__free_r(struct _reent *context, void *pointer)
{
    int outer = allocator_depth++ == 0;
    if (outer) forget(pointer);
    program_free(context, pointer);
    --allocator_depth;
}

void *__wrap__realloc_r(struct _reent *context, void *pointer, size_t size)
{
    if (pointer == NULL) return __wrap__malloc_r(context, size);
    if (size == 0) { __wrap__free_r(context, pointer); return NULL; }
    int outer = allocator_depth++ == 0;
    struct allocation **link = outer ? find(pointer) : NULL;
    struct allocation *reserved = NULL;
    if (outer && !*link && rp2040_soft_active() && context == &rp2040_program_reent) {
        reserved = __real__malloc_r(_GLOBAL_REENT, sizeof(*reserved));
        if (!reserved) { --allocator_depth; _REENT_ERRNO(context) = ENOMEM; return NULL; }
    }
    void *result = program_realloc(context, pointer, size);
    /* Failed realloc leaves the original block and its ownership intact. */
    if (outer && result) {
        if (*link) (*link)->pointer = result;
        else if (reserved) {
            reserved->pointer = result;
            reserved->next = allocations;
            allocations = reserved;
            reserved = NULL;
        }
    }
    if (reserved) __real__free_r(_GLOBAL_REENT, reserved);
    --allocator_depth;
    return result;
}

size_t rp2040_soft_live_allocations(void)
{
    size_t count = 0;
    for (struct allocation *entry = allocations; entry; entry = entry->next) ++count;
    return count;
}

void rp2040_soft_libc_reclaim(void)
{
    while (allocations) {
        struct allocation *entry = allocations;
        allocations = entry->next;
        program_free(_GLOBAL_REENT, entry->pointer);
        __real__free_r(_GLOBAL_REENT, entry);
    }
}
