/*
 * Heap and stack high-water measurement.
 *
 * The RP2040 has 264 KB of SRAM and no memory protection.  The linker lays
 * out static data, then the heap, then a fixed reserve above the heap that the
 * stack grows down into; the heap/reserve split is a manifest decision
 * (HEAP_END / STACK_RESERVE).  Get it wrong and the stack silently scribbles
 * on the heap rather than faulting, so measure both peaks:
 *
 *   heap  - newlib's malloc only ever grows the arena, so
 *           (__HeapLimit - _sbrk(0)) is already a running worst case: the
 *           fewest bytes that were free at any point, for free, from one
 *           _sbrk(0) call.  It is the space left to grow, not counting free
 *           blocks the allocator is holding.
 *   stack - paint the unused part of the reserve at startup (this file's
 *           constructor runs early, before the main stack gets deep) and scan
 *           up from the bottom for the first byte the stack has disturbed.
 *           The stack grows down, so the deepest disturbed byte is the peak.
 *
 * A sample is taken and logged to a file on the volume at each level change -
 * which is when NetHack allocates most - so the numbers survive an
 * out-of-memory panic; the session peaks are also printed as the game exits.
 * This is measurement scaffolding: it costs one 20 KiB memset at startup, one
 * bounded summary write per level, and one 20 KiB scan per level.
 */
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

extern char __HeapLimit; /* top of the heap, bottom of the stack reserve */
extern char __StackTop;  /* top of SRAM; the stack grows down from here */
extern char end;         /* bottom of the heap (end of .bss) */
extern void *_sbrk(int);

#define NH_PAINT 0xA5  /* must not be a value the stack commonly writes */
#define NH_MARGIN 512  /* leave the frames live at paint time untouched */
#define NH_LOGFILE "/memlog"

static unsigned char *paint_lo;
static unsigned char *paint_hi;
static unsigned long heap_min_free = ~0UL; /* lowest free heap seen */
static unsigned long stack_peak;           /* deepest stack, from __StackTop */
static int reported;

static uintptr_t
nh_sp(void)
{
    uintptr_t sp;

    __asm__ volatile("mov %0, sp" : "=r"(sp));
    return sp;
}

/* Bytes malloc can still grow into.  Monotonic: the arena never shrinks. */
static unsigned long
nh_heap_free(void)
{
    uintptr_t top = (uintptr_t) _sbrk(0);
    uintptr_t limit = (uintptr_t) &__HeapLimit;

    return (top <= limit) ? (unsigned long) (limit - top) : 0UL;
}

static unsigned long
nh_stack_used(void)
{
    unsigned char *p;

    if (!paint_lo)
        return 0UL;
    for (p = paint_lo; p < paint_hi && *p == NH_PAINT; p++)
        ;
    /* p is the deepest byte the stack has touched (or the paint top). */
    return (unsigned long) ((uintptr_t) &__StackTop - (uintptr_t) p);
}

static void
nh_mem_init(void)
{
    uintptr_t lo = (uintptr_t) &__HeapLimit;
    uintptr_t hi = (uintptr_t) &__StackTop;
    uintptr_t sp = nh_sp();

    heap_min_free = nh_heap_free();
    if (sp > hi)
        sp = hi;
    if (sp <= lo + NH_MARGIN)
        return;
    paint_lo = (unsigned char *) lo;
    paint_hi = (unsigned char *) (sp - NH_MARGIN);
    memset(paint_lo, NH_PAINT, (size_t) (paint_hi - paint_lo));
}

static void
nh_mem_sample(void)
{
    unsigned long f = nh_heap_free();
    unsigned long s = nh_stack_used();

    if (f < heap_min_free)
        heap_min_free = f;
    if (s > stack_peak)
        stack_peak = s;
}

/* Sample peaks on level changes and retain the latest summary in a bounded
 * volume log, so an out-of-memory panic does not lose the low-water marks. */
void
nh_mem_transition(int dlevel, long turn)
{
    char buf[160];
    int n, fd;

    nh_mem_sample();
    n = snprintf(buf, sizeof buf,
                 "dlvl %d turn %ld heap_free_low %lu stack_peak %lu\n",
                 dlevel, turn, heap_min_free, stack_peak);
    if (n <= 0)
        return;
    fd = open(NH_LOGFILE, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0)
        return;
    (void) write(fd, buf, (size_t) n);
    (void) close(fd);
}

/* Print the session peaks.  Called as the game exits and on a panic. */
void
nh_mem_report(void)
{
    unsigned long budget = (unsigned long) ((uintptr_t) &__HeapLimit
                                            - (uintptr_t) &end);
    unsigned long used;

    if (reported)
        return;
    reported = 1;
    nh_mem_sample();
    used = (budget > heap_min_free) ? budget - heap_min_free : 0UL;
    (void) printf("\nRAM: heap %lu B, low-water free %lu B (%lu%% used); "
                  "stack peak %lu B of the %lu B reserve\n",
                  budget, heap_min_free,
                  budget ? (100UL * used / budget) : 0UL, stack_peak,
                  (unsigned long) ((uintptr_t) &__StackTop
                                   - (uintptr_t) &__HeapLimit));
}

__attribute__((constructor))
static void
nh_mem_ctor(void)
{
    nh_mem_init();
}
