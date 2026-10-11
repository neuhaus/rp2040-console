/*
 * Start a new game when this one ends, instead of leaving the board dark.
 *
 * A game finishes by calling exit(), from more places than one: the normal
 * quit, death, a save, cleanup on error, and whatever the program does when
 * it decides it has had enough. There is no kernel to return to, so the
 * SDK's own _exit() disabled interrupts and spun, and a finished game sat
 * there until the board was power-cycled.
 *
 * Overriding _exit() rather than exit() is what catches all of those at
 * once, and it is the only one of the two that can be overridden here:
 * _exit() is a weak symbol in the SDK, so a strong definition replaces it
 * without a duplicate-definition error, while exit() is strong. libc's
 * exit() runs the atexit handlers and flushes before calling _exit(), so
 * hooking the inner one misses nothing that hooking the outer one would
 * have caught - and it also catches a direct _exit().
 *
 * Restarting is a full reboot, not another call to main(). A game is a
 * single-shot program: it allocates a world and never gives it back, on the
 * reasonable assumption that exit() is the end. Looping main() would strand
 * another world's worth of heap each time and eventually run out, which is
 * a much worse outcome than one reboot per game. A reboot costs about a
 * second and hands the next game all of its RAM.
 */

#include <stdio.h>
#include <unistd.h>

#include "pico/stdlib.h"
#include "hardware/watchdog.h"

#include "clock.h"
#include "restart.h"

/*
 * How long the program has to have been running for an exit() to count as
 * the end of a game.
 *
 * Every real startup fault found in this port so far - the TERM lookup,
 * the lock files, the volume mount, the unwired stdio - reached exit()
 * within a tenth of a second of boot, before there was anything on screen
 * to lose. Rebooting on those would reprint the error faster than it can
 * be read and leave the board looking merely broken rather than wrong in
 * a way anybody can diagnose.
 *
 * Three seconds is well clear of all of those, and well short of the time
 * it takes to reach gameplay, answer "Who are you?" and quit.
 */
#define BOOT_GRACE_US (3 * 1000000ull)

/*
 * Longest wait for the key that starts the next game. Normally the wait
 * ends the moment a key arrives; this exists only so that a board with
 * nothing attached to it still gets on with rebooting instead of hanging
 * in the dark forever.
 */
#define RESTART_KEY_TIMEOUT_US (10 * 60 * 1000000ull)

static uint64_t boot_us;

void restart_note_boot(void)
{
    boot_us = time_us_64();
}

void _exit(int status)
{
    uint64_t started = boot_us;
    uint64_t deadline;

    (void)status;

    /*
     * Too young to have been a game. Say so and stop, rather than
     * rebooting into the same failure: the message stays on screen and
     * the board stays usable. started == 0 means restart_note_boot()
     * never ran, which is the worst case of this, not an exemption.
     */
    if (started == 0 || time_us_64() - started < BOOT_GRACE_US) {
        printf("\nThe program stopped during startup, so it is not "
               "restarting.\nPower cycle the board to try again.\n");
        for (;;)
            sleep_ms(1000);
    }

    /*
     * Wait for the player, deliberately: the end-of-game report and the
     * top-ten list are the point of finishing, and rebooting into a new
     * game would scroll them away. It also keeps the reboot the player's
     * own decision instead of something that happens to the serial
     * session underneath them.
     *
     * The wait is bounded so that a board with no console still restarts.
     * EOF is not treated as "no console" and bailed out on, because a
     * stdio driver with no character ready reports EOF right away, which
     * is the ordinary state of a healthy link and not a reason to skip
     * the wait.
     */
    printf("\nPress any key for a new game...\n");

    deadline = time_us_64() + RESTART_KEY_TIMEOUT_US;
    while (time_us_64() < deadline) {
        if (getchar() != EOF)
            break;
        sleep_ms(20);
    }

    /*
     * start == 0 lets the bootrom choose, which for an image running from
     * flash means booting this same program again. The FAT volume lives
     * at 0x180000 and is not touched by a reboot, so the record file and
     * any saved game survive it.
     *
     * The session time is stored first, so the next boot offers
     * when this one ended rather than the build date.
     */
    console_clock_persist();
    console_clock_stash();
    watchdog_reboot(0, NULL, 0);

    /* Not reached: the watchdog fires within a millisecond. */
    for (;;)
        ;
}
