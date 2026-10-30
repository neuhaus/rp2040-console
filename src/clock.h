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
 * The same clock with microseconds, for gettimeofday(). The seconds always
 * agree with console_clock_now(), so a program comparing a time it took
 * itself with a file's mtime sees one timeline rather than two.
 */
void console_clock_now_us(time_t *sec, long *usec);

/*
 * Unix seconds for a calendar date and time, with no timezone applied: the
 * same reckoning the clock uses for the date typed at startup, so a FAT
 * stamp converted with it compares directly with time().
 */
time_t console_clock_epoch_of(int y, int mo, int d, int hh, int mm, int ss);

/*
 * Store the current session time on the volume, to be offered as the default
 * next boot. Called once the date is settled and again before rebooting.
 */
void console_clock_persist(void);

/*
 * Hand the session time to the next boot through the watchdog scratch
 * registers (see clock.c). Call immediately before watchdog_reboot and
 * nowhere else: the marker means "this boot follows directly", which is
 * only true there.
 */
void console_clock_stash(void);

#endif /* RP2040_CONSOLE_CLOCK_H */
