#ifndef RP2040_CONSOLE_CLOCK_H
#define RP2040_CONSOLE_CLOCK_H

#include <time.h>

/*
 * Ask for the date and time on the console. Blank input - just Enter - is
 * accepted and uses the default. Returns once there is something usable,
 * so it is safe to call unconditionally during startup.
 */
void console_clock_init(void);

/*
 * Seconds since the Unix epoch: whatever was accepted at startup, plus the
 * time since the board booted. Clocks forward while the program runs and
 * restarts from the default on every boot; there is no battery anywhere on
 * a Pico to keep it alive.
 */
time_t console_clock_now(void);

/*
 * Store the current session time on the volume, to be offered as the default
 * next boot. Called once the date is settled and again before rebooting.
 */
void console_clock_persist(void);

#endif /* RP2040_CONSOLE_CLOCK_H */
