/*
 * Host stand-in for <pico/stdlib.h>, for the console input tests only.
 *
 * src/console.c includes it for the SDK's umbrella of platform headers; the
 * pieces the console actually calls come from <pico/stdio.h> and <pico/time.h>,
 * which have their own stubs here. The header only has to exist.
 */
#ifndef RP2040_CONSOLE_TEST_PICO_STDLIB_H
#define RP2040_CONSOLE_TEST_PICO_STDLIB_H

#endif /* RP2040_CONSOLE_TEST_PICO_STDLIB_H */
