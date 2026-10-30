/*
 * Resource-validation hooks for the ROGUE_DIAG firmware. Not for release.
 *
 * Rogue's sources stay untouched: the link wraps rgetchar() (-Wl,--wrap), the
 * one routine every command and prompt reads keys through. Each call samples
 * memory, so the peaks cover the whole session between keystrokes too:
 *
 *   heap  - newlib's arena only grows, so __HeapLimit - _sbrk(0) is the
 *           fewest bytes that were ever left to grow into. mallinfo() adds
 *           bytes actually allocated, sampled for a running peak.
 *   stack - the reserve below the stack is painted on the first call and
 *           scanned upward for the deepest byte disturbed since.
 *
 * Typing ^] (0x1d) prints one "DIAG ..." line with the samples and the
 * game's object/monster/pack counts; ^^ (0x1e) prints the dungeon's cell
 * flags and the player's position, so a test can path-find with the game's
 * own movement rules. Both redraw the screen afterwards and are not passed
 * on, so the game never sees them.
 */
#include <malloc.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "rogue.h"

#define DIAG_KEY  0x1d
#define MAP_KEY   0x1e
#define PAINT     0xA5
#define MARGIN    256

extern char __HeapLimit, __StackTop, end;
extern void *_sbrk(int);
extern object level_objects, level_monsters;
extern object *free_list;
extern fighter rogue;
extern short cur_level, max_level;
extern unsigned short dungeon[DROWS][DCOLS];
extern boolean wizard;
extern WINDOW *curscr;

int __real_rgetchar(void);

static unsigned char *paint_lo, *paint_hi;
static unsigned long heap_free_min = ~0UL, inuse_peak, stack_peak, samples;

static uintptr_t current_sp(void)
{
    uintptr_t sp;

    __asm__ volatile("mov %0, sp" : "=r"(sp));
    return sp;
}

static void paint(void)
{
    paint_lo = (unsigned char *)&__HeapLimit;
    paint_hi = (unsigned char *)(current_sp() - MARGIN);
    if (paint_hi > paint_lo)
        memset(paint_lo, PAINT, (size_t)(paint_hi - paint_lo));
}

static void sample(void)
{
    uintptr_t top = (uintptr_t)_sbrk(0), limit = (uintptr_t)&__HeapLimit;
    unsigned long room = top <= limit ? (unsigned long)(limit - top) : 0;
    struct mallinfo mi = mallinfo();
    unsigned char *p;

    if (room < heap_free_min)
        heap_free_min = room;
    if ((unsigned long)mi.uordblks > inuse_peak)
        inuse_peak = (unsigned long)mi.uordblks;
    for (p = paint_lo; p < paint_hi && *p == PAINT; p++)
        ;
    if ((unsigned long)((uintptr_t)&__StackTop - (uintptr_t)p) > stack_peak)
        stack_peak = (unsigned long)((uintptr_t)&__StackTop - (uintptr_t)p);
    samples++;
}

static int count(const object *list)
{
    int n = 0;

    for (list = list->next_object; list; list = list->next_object)
        n++;
    return n;
}

static void report(void)
{
    struct mallinfo mi = mallinfo();
    const object *o;
    int amulet = 0, free_n = 0;

    for (o = rogue.pack.next_object; o; o = o->next_object)
        if (o->what_is == AMULET)
            amulet = 1;
    for (o = free_list; o; o = o->next_object)
        free_n++;
    printf("\r\nDIAG level=%d max=%d wizard=%d objects=%d monsters=%d "
           "pack=%d amulet=%d freelist=%d hp=%d heap_inuse=%lu "
           "heap_inuse_peak=%lu arena=%lu heap_free_min=%lu "
           "stack_peak=%lu stack_reserve=%lu samples=%lu END\r\n",
           cur_level, max_level, wizard, count(&level_objects),
           count(&level_monsters), count(&rogue.pack), amulet, free_n,
           rogue.hp_current, (unsigned long)mi.uordblks, inuse_peak,
           (unsigned long)mi.arena, heap_free_min, stack_peak,
           (unsigned long)((uintptr_t)&__StackTop - (uintptr_t)&__HeapLimit),
           samples);
    fflush(stdout);
    wrefresh(curscr);
}

static void dump_map(void)
{
    const object *o;
    int r, c;

    printf("\r\nMAP pos=%d,%d level=%d\r\n", rogue.row, rogue.col, cur_level);
    for (r = 0; r < DROWS; r++) {
        printf("M%02d ", r);
        for (c = 0; c < DCOLS; c++)
            printf("%04x", dungeon[r][c]);
        printf("\r\n");
    }
    for (o = level_objects.next_object; o; o = o->next_object)
        printf("OBJ %d,%d %o\r\n", o->row, o->col, o->what_is);
    printf("MAPEND\r\n");
    fflush(stdout);
    wrefresh(curscr);
}

int __wrap_rgetchar(void)
{
    if (!paint_lo)
        paint();
    for (;;) {
        int ch;

        sample();
        ch = __real_rgetchar();
        sample();
        if (ch == DIAG_KEY)
            report();
        else if (ch == MAP_KEY)
            dump_map();
        else
            return ch;
    }
}
