/*
 * Optional console tracing for the port.
 *
 * Off by default. Everything gated here traces normal operation - which
 * path was opened with which flags, which sector FatFs asked for - and the
 * game owns the console, so that is text in the player's way. Two things
 * make it worse than merely chatty: a failed open is a routine query here
 * (the game looks for a save file before deciding to write one, so
 * FR_NO_FILE arrives on the happy path), and every byte competes with the
 * game's own output on the same USB link.
 *
 * Build with -DPICO_IO_TRACE=ON to have them when something needs
 * diagnosing. Genuine setup failures - no volume, wrong filesystem, mount
 * refused - stay unconditional, because those have no other way of saying
 * anything.
 */
#ifndef RP2040_CONSOLE_TRACE_H
#define RP2040_CONSOLE_TRACE_H

#ifdef PICO_IO_TRACE
#include <stdio.h>
#define TRACE(...) printf(__VA_ARGS__)
#else
/* Arguments are not evaluated, so no stdio dependency and no cost. */
#define TRACE(...) ((void)0)
#endif

#endif /* RP2040_CONSOLE_TRACE_H */
