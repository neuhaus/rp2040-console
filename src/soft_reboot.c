/* Single-threaded session boundary. Never call exit/allocators from IRQs or
 * core 1 in this mode. All newlib allocations belong to a reentrancy context,
 * so libc-owned buffers and leaked program objects are reclaimed together. */
#include <errno.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdlib.h>
#include "rp2040-soft-reboot.h"
#include "soft_reboot_internal.h"

struct allocation {
    void *pointer;
    struct allocation *next;
};

static struct allocation *allocations;
static jmp_buf exit_boundary;
static int active, exit_status, run_handlers;
static int cleaning;
static unsigned allocator_depth;
static void (*handlers[32])(void);
static size_t handler_count;

extern void *__real__malloc_r(struct _reent *, size_t);
extern void *__real__calloc_r(struct _reent *, size_t, size_t);
extern void *__real__realloc_r(struct _reent *, void *, size_t);
extern void __real__free_r(struct _reent *, void *);

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
    if (pointer == NULL || !active || context != &rp2040_program_reent)
        return pointer;
    struct allocation *entry = __real__malloc_r(_GLOBAL_REENT, sizeof(*entry));
    if (entry == NULL) {
        __real__free_r(context, pointer);
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
    void *pointer = __real__malloc_r(context, size);
    if (outer) pointer = remember(context, pointer);
    --allocator_depth;
    return pointer;
}

void *__wrap__calloc_r(struct _reent *context, size_t count, size_t size)
{
    if (size && count > SIZE_MAX / size) { _REENT_ERRNO(context) = ENOMEM; return NULL; }
    int outer = allocator_depth++ == 0;
    void *pointer = __real__calloc_r(context, count, size);
    if (outer) pointer = remember(context, pointer);
    --allocator_depth;
    return pointer;
}

void __wrap__free_r(struct _reent *context, void *pointer)
{
    int outer = allocator_depth++ == 0;
    if (outer) forget(pointer);
    __real__free_r(context, pointer);
    --allocator_depth;
}

void *__wrap__realloc_r(struct _reent *context, void *pointer, size_t size)
{
    if (pointer == NULL) return __wrap__malloc_r(context, size);
    if (size == 0) { __wrap__free_r(context, pointer); return NULL; }
    int outer = allocator_depth++ == 0;
    struct allocation **link = outer ? find(pointer) : NULL;
    struct allocation *reserved = NULL;
    if (outer && !*link && active && context == &rp2040_program_reent) {
        reserved = __real__malloc_r(_GLOBAL_REENT, sizeof(*reserved));
        if (!reserved) { --allocator_depth; _REENT_ERRNO(context) = ENOMEM; return NULL; }
    }
    void *result = __real__realloc_r(context, pointer, size);
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

int rp2040_soft_active(void) { return active; }

size_t rp2040_soft_live_allocations(void)
{
    size_t count = 0;
    for (struct allocation *entry = allocations; entry; entry = entry->next) ++count;
    return count;
}

int rp2040_soft_atexit(void (*handler)(void))
{
    if (!active || handler == NULL || handler_count == 32) { errno = ENOMEM; return -1; }
    handlers[handler_count++] = handler;
    return 0;
}

static void finish(int status, int normal) __attribute__((noreturn));
static void finish(int status, int normal)
{
    if (!active || cleaning) rp2040_soft_fatal(status);
    exit_status = status;
    run_handlers = normal;
    /* Run handlers and flush FILE buffers before unwinding the program stack.
     * Otherwise a handler/buffer referring to live automatic storage in a
     * caller of exit() would already be dangling. Pop before calling, so a
     * nested exit() from a handler runs only the remaining handlers. */
    while (run_handlers && handler_count) handlers[--handler_count]();
    cleaning = 1;
    if (rp2040_soft_libc_end(run_handlers) != 0) rp2040_soft_fatal(127);
    rp2040_soft_platform_cleanup();
    longjmp(exit_boundary, 1);
}

void rp2040_soft_exit(int status) { finish(status, 1); }

void rp2040_soft_immediate_exit(int status)
{
    finish(status, 0);
}

extern int rp2040_console_game_main(int argc, char **argv);

int rp2040_soft_run(int argc, char **argv)
{
    if (active) rp2040_soft_fatal(127);
    rp2040_soft_restore();
    handler_count = 0;
    exit_status = 0;
    run_handlers = 0;
    cleaning = 0;
    active = 1;
    if (setjmp(exit_boundary) == 0) {
        if (rp2040_soft_libc_begin() != 0) rp2040_soft_immediate_exit(127);
        rp2040_soft_exit(rp2040_console_game_main(argc, argv));
    }
    active = 0;
    while (allocations) {
        struct allocation *entry = allocations;
        allocations = entry->next;
        __real__free_r(_GLOBAL_REENT, entry->pointer);
        __real__free_r(_GLOBAL_REENT, entry);
    }
    return exit_status;
}
