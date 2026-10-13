/*
 * Engine entry point for a terminal game on RP2040.
 *
 * This file owns main(). The program's own main is renamed to
 * rp2040_console_game_main by the prefix header, so nothing has to be added to
 * game sources to bring the console, the volume or the clock up first: every
 * program linked with the engine gets the same startup, and a game cannot
 * forget to call it.
 *
 * Owning the entry point also settles the arguments, which is not a detail. The
 * SDK's C runtime branches to main without setting up r0 and r1, so argc and
 * argv as a program receives them are whatever initialisation left behind. The
 * engine replaces that with one argument, the program's own name, because a
 * program legitimately wants argv[0] - to name itself in messages, or to stat
 * itself to find out when it was built - and there is no command line here to
 * give it anything else.
 */

#include <stdio.h>
#include <stdint.h>
#include <sys/random.h>

#include <pico/stdlib.h>
#include <pico/stdio.h>
#include <pico/stdio_usb.h>
#include <pico/time.h>

#ifndef RP2040_CONSOLE_CONNECT_WAIT_MS
#define RP2040_CONSOLE_CONNECT_WAIT_MS 0
#endif

#include "console.h"
#include "diag.h"
#include "env.h"
#include "storage.h"
#include "probes.h"
#include "restart.h"
#include "clock.h"
#ifdef RP2040_CONSOLE_SOFT_REBOOT
#include "rp2040-soft-reboot.h"
#endif

static void pid_init(void);

/*
 * Everything the engine needs before the game runs.
 *
 *   stdio_init_all()   - console before anything that prints; see below
 *   env_init()         - the environment the game reads through getenv(),
 *                        chiefly TERM. A game that asks for TERM and gets
 *                        NULL tends to treat it as fatal, and it asks early.
 *   storage_mount()    - the FAT volume, so the game's data files can be read
 *                        and saves written. Before the clock, because the
 *                        clock's default comes partly from a file on the
 *                        volume. Failure is not fatal; the game reports what
 *                        it could not do.
 *   console_clock_init() - the date and time, offered for correction on the
 *                        console. Nothing here has a clock, and a game that
 *                        stamps score files will otherwise stamp them with
 *                        1970.
 *
 * An __attribute__((constructor)) is deliberately not used for this. It would
 * not run: this SDK's startup calls runtime_init, main and exit, and does not
 * walk .init_array at all - __libc_init_array is not even in the link. An
 * initialiser that is recorded and never called is close to impossible to spot,
 * which is the argument against constructors wherever they are unreliable. A
 * main() that the runtime calls by name is visible in the link map.
 */
static void engine_start(void)
{
    /*
     * Stamp the start time first, so that _exit() can measure against the real
     * beginning of the program rather than against however far startup happened
     * to get. See restart.c.
     */
    restart_note_boot();

    /*
     * The LED starts reporting the stage reached before anything else can go
     * wrong, and keeps reporting it from an alarm interrupt afterwards.
     */
    console_diag_init();

    /*
     * First statement that prints: bring up UART0, so the progress markers are
     * visible even if USB never enumerates.
     */
    probe_init();
    probe(2);

    /*
     * pico_enable_stdio_usb() only arranges for pico_stdio_usb to be linked; it
     * does not initialise it. In SDK 2.3.1 nothing calls stdio_init_all() on
     * the application's behalf. Without this call stdio_usb_init() is never
     * reached, --gc-sections drops the whole driver, no tud_task() ever runs,
     * and the board never enumerates on USB at all.
     *
     * stdio_init_all() also sets up UART0, leaving a wired console.
     */
    stdio_init_all();
    console_diag_stage(DIAG_STAGE_CONSOLE);

    /*
     * Nothing has been printed yet, and that is deliberate: give a terminal a
     * chance to open first, or the startup and the first prompt go out into the
     * absence and the board looks dead. See console_wait_for_terminal().
     */
    console_wait_for_terminal(RP2040_CONSOLE_CONNECT_WAIT_MS);

    /*
     * Unbuffered, deliberately, and it has to happen before anything prints.
     *
     * The SDK puts putchar(), puts() and printf() on their way to the USB
     * driver by --wrapping them, which emits immediately. fputs(), fprintf()
     * and fwrite() are not wrapped: they stay inside newlib's stdio and reach
     * _write(). Left at its default buffering, stdout collects those in a 4KB
     * buffer while the wrapped calls go straight out, and a program that mixes
     * them - which is almost all of them - sees its own output arrive in the
     * wrong order. With no buffering both paths reach the console in the order
     * the program wrote them.
     */
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

#ifdef RP2040_CONSOLE_UART_STDIN
    console_uart_stdin_init();
#endif
    probe(3);

    env_init();
    console_diag_stage(DIAG_STAGE_ENV);
    probe(4);

    /*
     * The volume before the clock: the clock's default is the later of the
     * build date and the time the last session ended, and the latter lives
     * in a file on the volume. Nothing here needs the time to mount.
     */
    storage_mount();
    console_diag_stage(DIAG_STAGE_STORAGE);
    probe(5);

    console_clock_init();
    console_diag_stage(DIAG_STAGE_CLOCK);
    probe(6);

    /* A pid the locks can distinguish from last boot's; see below. */
    pid_init();

    console_diag_stage(DIAG_STAGE_READY);
}

/*
 * The program's own main, under the name the prefix header gives it. A link
 * with no main to rename fails on this symbol, which is the point: the
 * alternative is a program that starts without ever being set up.
 */
extern int rp2040_console_game_main(int argc, char **argv);

/*
 * The entry point. The C runtime calls this because it is the only main() in
 * the link; the program's own was renamed to rp2040_console_game_main.
 *
 * argc and argv are ignored rather than forwarded: on this device the runtime
 * has no arguments to pass, and leaves whatever initialisation put in r0 and
 * r1. Forwarding those would hand the program a pointer it will dereference.
 */
int main(int argc, char **argv)
{
#ifndef RP2040_CONSOLE_SOFT_REBOOT
    static char *console_argv[] = { RP2040_CONSOLE_ARGV0, NULL };
#endif

    (void)argc;
    (void)argv;

    engine_start();

#ifdef RP2040_CONSOLE_SOFT_REBOOT
    for (;;) {
        /* New stack frames and argv each time; USB/clock/storage init run once. */
        char name[] = RP2040_CONSOLE_ARGV0;
        char *argv[] = { name, NULL };
        restart_note_boot();
        pid_init();
        int status = rp2040_soft_run(1, argv);
        restart_wait_soft(status);
    }
#else
    return rp2040_console_game_main(1, console_argv);
#endif
}

/*
 * One process, so the pid only has to distinguish this boot from the last:
 * lock files record the pid that took them, and a lock whose pid is gone
 * means its game is gone. Stable within a boot, fresh across them.
 */
static int boot_pid = 0;

static void pid_init(void)
{
    uint32_t r = 0;

    if (getentropy(&r, sizeof(r)) != 0 || r == 0)
        r = (uint32_t)time_us_64();
    int next = (int)(r % 32767u) + 1;
    if (next == boot_pid) next = next % 32767 + 1;
    boot_pid = next;
}

int getpid(void)
{
    return boot_pid != 0 ? boot_pid : 1;
}
