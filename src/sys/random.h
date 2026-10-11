/*
 * Stand-in for <sys/random.h>, which newlib for RP2040 does not ship.
 *
 * A program that seeds itself with getentropy() usually pulls in this
 * header to get the declaration, and newlib for RP2040 does not have it.
 * The engine provides the function in entropy.c, over the Pico's own
 * entropy sources, so the declaration has to come from somewhere - from
 * here.
 *
 * The include guard is deliberately not the usual _SYS_RANDOM_H: if a
 * libc ever does grow this header, the guard should not make ours hide it.
 */
#ifndef RP2040_CONSOLE_SYS_RANDOM_H
#define RP2040_CONSOLE_SYS_RANDOM_H

#include <stddef.h>

int getentropy(void *buffer, size_t length);

#endif /* RP2040_CONSOLE_SYS_RANDOM_H */
