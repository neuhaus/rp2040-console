#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

static int initialized = 27;
static int zeroed;
static void at_end(void) { puts("soft session handler"); }

int main(int argc, char **argv)
{
    (void)argc; (void)argv;
    assert(initialized == 27 && zeroed == 0);
    initialized = 99;
    zeroed = 99;
    void *leaked = malloc(4096);
    assert(leaked != NULL);
    assert(atexit(at_end) == 0);
    FILE *file = fopen("session.txt", "w");
    assert(file != NULL);
    fputs("soft session\n", file);
    /* Leave allocations and the stream open: the platform must reclaim them. */
    return 0;
}
