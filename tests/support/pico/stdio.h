/*
 * Host stand-in for the SDK's stdio, for the console input tests only.
 *
 * tests/support precedes the SDK on the include path when tests/run.sh builds
 * tests/console-input.c, so compiling src/console.c against a scripted device
 * needs only the entry points that file calls, plus the error codes and the
 * getchar_timeout_us() alias the SDK declares here.
 */
#ifndef RP2040_CONSOLE_TEST_PICO_STDIO_H
#define RP2040_CONSOLE_TEST_PICO_STDIO_H

#include <stdbool.h>
#include <stdint.h>

int stdio_getchar(void);
int stdio_getchar_timeout_us(uint32_t timeout_us);
void stdio_put_string(const char *s, int len, bool newline, bool flush);

/* As pico/error.h defines them. */
enum {
    PICO_ERROR_TIMEOUT = -2,
    PICO_ERROR_NO_DATA = -3,
};

/* As pico/stdio.h declares it: an alias for stdio_getchar_timeout_us(). */
static inline int getchar_timeout_us(uint32_t timeout_us)
{
    return stdio_getchar_timeout_us(timeout_us);
}

#endif /* RP2040_CONSOLE_TEST_PICO_STDIO_H */
