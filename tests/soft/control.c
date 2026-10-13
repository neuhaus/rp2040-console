#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <reent.h>
#include <unistd.h>
#include <locale.h>
#include <time.h>
#include <errno.h>
#include <limits.h>
#include "rp2040-soft-reboot.h"
#include "soft_reboot_internal.h"

int test_mode, test_callbacks, test_callback_order;
char test_timezone[16] = "TZ=UTC0";
static char *test_environment[] = { test_timezone, NULL };
char **environ = test_environment;
static unsigned restores, cleanups;
extern unsigned char test_program_data_start[], test_program_data_end[];
extern unsigned char test_program_bss_start[], test_program_bss_end[];
static unsigned char initial[1024];
static int captured;
extern size_t test_heap_break(void);

void rp2040_soft_restore(void)
{
    size_t data_size = (uintptr_t)test_program_data_end - (uintptr_t)test_program_data_start;
    size_t bss_size = (uintptr_t)test_program_bss_end - (uintptr_t)test_program_bss_start;
    assert(data_size <= sizeof initial && bss_size > 0);
    if (!captured) { memcpy(initial, test_program_data_start, data_size); captured = 1; }
    memcpy(test_program_data_start, initial, data_size);
    memset(test_program_bss_start, 0, bss_size);
    ++restores;
}
void rp2040_soft_platform_cleanup(void)
{
    strcpy(test_timezone, "TZ=UTC0");
    tzset();
    setlocale(LC_ALL, "C");
    ++cleanups;
}
void rp2040_soft_fatal(int status) { fprintf(stderr, "fatal cleanup %d\n", status); _exit(status); }

int test_main(void)
{
    printf("ARM/newlib soft reboot lifecycle test\n");
    char *persistent = malloc(64);
    assert(persistent != NULL);
    strcpy(persistent, "platform allocation survives");
    struct _reent *platform = _impure_ptr;
    FILE *console = stdout;
    size_t stable_break = 0;
    for (unsigned i = 0; i < 60; ++i) {
        test_mode = i % 6;
        test_callbacks = test_callback_order = 0;
        char *argv[] = { "soft-test", NULL };
        int status = rp2040_soft_run(1, argv);
        assert(status == (test_mode == 0 ? 7 : test_mode == 1 ? 42 : test_mode == 2 ? 24 : test_mode == 3 ? 43 : test_mode == 4 ? 17 : 25));
        assert(test_callbacks == ((test_mode == 2 || test_mode == 5) ? 0 : test_mode == 3 ? 3 : 2));
        if (test_mode != 2 && test_mode != 5) assert(test_callback_order == 21);
        assert(rp2040_soft_live_allocations() == 0);
        assert(_impure_ptr == platform && stdout == console);
        assert(strcmp(persistent, "platform allocation survives") == 0);
        FILE *file = fopen("soft-test.txt", "r");
        assert(file != NULL);
        char content[32];
        if (test_mode == 2 || test_mode == 5) assert(fgets(content, sizeof content, file) == NULL);
        else assert(fgets(content, sizeof content, file) != NULL && strcmp(content, "persisted\n") == 0);
        fclose(file);
        if (i == 5) stable_break = test_heap_break();
        if (i > 5) assert(test_heap_break() == stable_break);
    }
    extern char __HeapLimit;
    extern void *_sbrk(int);
    uintptr_t current = (uintptr_t)_sbrk(0);
    errno = 0;
    assert(_sbrk((int)((uintptr_t)&__HeapLimit - current + 1)) == (void *)-1 && errno == ENOMEM);
    assert((uintptr_t)_sbrk(0) == current);
    errno = 0;
    assert(_sbrk(INT_MIN) == (void *)-1 && errno == ENOMEM);
    assert((uintptr_t)_sbrk(0) == current);
    free(persistent);
    assert(restores == 60 && cleanups == 60);
    puts("PASS: 60 ARM/newlib sessions, globals, allocations, streams, handlers, _exit, platform state, stable heap high-water");
    return 0;
}
