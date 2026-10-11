#ifndef RP2040_CONSOLE_PROBES_H
#define RP2040_CONSOLE_PROBES_H

/*
 * UART0 startup markers. See probes.c - these exist because "no USB
 * device" cannot be distinguished from "crashed before main()" by
 * looking at USB alone.
 */

/* Bring up UART0 and announce that the C runtime was reached. */
void probe_init(void);

/* Emit "[N] " on UART0 for stage N (1..6). */
void probe(int stage);

#endif /* RP2040_CONSOLE_PROBES_H */