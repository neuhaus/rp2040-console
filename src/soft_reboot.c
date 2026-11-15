/* Single-threaded session boundary. Never call exit/allocators from IRQs or
 * core 1 in this mode. This file is C-library agnostic: the libc backend
 * attributes program allocations so that libc-owned buffers and leaked
 * program objects are reclaimed together at the boundary. */
#include <errno.h>
#include <setjmp.h>
#include "rp2040-soft-reboot.h"
#include "soft_reboot_internal.h"

static jmp_buf exit_boundary;
static int active, exit_status, run_handlers;
static int cleaning;
static void (*handlers[32])(void);
static size_t handler_count;

int rp2040_soft_active(void) { return active; }

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
    rp2040_soft_libc_reclaim();
    return exit_status;
}
