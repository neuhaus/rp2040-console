#ifndef RP2040_CONSOLE_RESTART_H
#define RP2040_CONSOLE_RESTART_H

#include <stdint.h>

/*
 * Record when the program started, so _exit() can tell the end of a game
 * from a failure during startup. Called once, as early as main() gets to.
 * See restart.c.
 */
void restart_note_boot(void);
void restart_wait_soft(int status);

#endif /* RP2040_CONSOLE_RESTART_H */
