#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <locale.h>
#include "rp2040-soft-reboot.h"
#include "soft_reboot_internal.h"
#define exit rp2040_soft_exit
#define _exit rp2040_soft_immediate_exit
#define _Exit rp2040_soft_immediate_exit
#define atexit rp2040_soft_atexit

static int initialized = 27;
static int zeroed;
static const unsigned char *automatic;
extern int test_mode, test_callbacks, test_callback_order;
extern char test_timezone[];
static void first(void) {
    if (automatic) for (size_t i = 0; i < 512; ++i) assert(automatic[i] == 0x5a);
    ++test_callbacks; test_callback_order = test_callback_order * 10 + 1;
}
static void second(void) { ++test_callbacks; test_callback_order = test_callback_order * 10 + 2; }
static void exiting(void) { ++test_callbacks; exit(43); }
static void signal_handler(int value) { (void)value; }

int rp2040_console_game_main(int argc, char **argv)
{
    assert(argc == 1 && argv[0] != NULL);
    assert(initialized == 27 && zeroed == 0);
    initialized = 99; zeroed = 99;
    assert(atexit(first) == 0 && atexit(second) == 0);
    assert(signal(SIGINT, signal_handler) != SIG_ERR);
    strcpy(test_timezone, "TZ=UTC1");
    time_t stamp = 0;
    struct tm tm;
    assert(localtime_r(&stamp, &tm) == &tm && tm.tm_hour == 23);
    assert(setlocale(LC_ALL, "C") != NULL);
    void *allocation = malloc(4096);
    assert(allocation != NULL);
    memset(allocation, 7, 4096);
    void *old = allocation;
    errno = 0;
    volatile size_t too_large = SIZE_MAX - 16;
    assert(realloc(allocation, too_large) == NULL);
    assert(((unsigned char *)old)[0] == 7);
    allocation = realloc(allocation, 8192);
    assert(allocation != NULL && ((unsigned char *)allocation)[0] == 7);
    void *small = calloc(16, 16);
    assert(small != NULL && ((unsigned char *)small)[0] == 0);
    free(small);
    char *string = strdup("libc-owned allocation");
    assert(string != NULL);
    /* Leave allocation and string live; ordinary libc malloc is tracked. */
    FILE *file = fopen("soft-test.txt", "w");
    assert(file != NULL);
    char live_value[512], live_buffer[4096];
    memset(live_value, 0x5a, sizeof live_value);
    if (test_mode == 1) {
        automatic = (unsigned char *)live_value;
        assert(setvbuf(file, live_buffer, _IOFBF, sizeof live_buffer) == 0);
    } else assert(setvbuf(file, NULL, _IOFBF, 4096) == 0);
    assert(fputs("persisted\n", file) >= 0);
    /* Deliberately leave FILE open: normal exit must flush, _exit must not. */
    assert(rp2040_soft_live_allocations() > 0);
    if (test_mode == 1) exit(42);
    if (test_mode == 2) _exit(24);
    if (test_mode == 3) { assert(atexit(exiting) == 0); exit(42); }
    if (test_mode == 4) { assert(fclose(stdout) == 0); assert(fclose(stdin) == 0); return 17; }
    if (test_mode == 5) _Exit(25);
    return 7;
}
