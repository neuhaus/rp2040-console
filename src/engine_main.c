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

#include <pico/stdlib.h>
#include <pico/stdio.h>
#include <pico/stdio_usb.h>

#include "console.h"
#include "env.h"
#include "storage.h"
#include "probes.h"
#include "restart.h"
#include "clock.h"

/*
 * Everything the engine needs before the game runs.
 *
 *   stdio_init_all()   - console before anything that prints; see below
 *   env_init()         - the environment the game reads through getenv(),
 *                        chiefly TERM. A game that asks for TERM and gets
 *                        NULL tends to treat it as fatal, and it asks early.
 *   console_clock_init() - the date and time, offered for correction on the
 *                        console. Nothing here has a clock, and a game that
 *                        stamps score files will otherwise stamp them with
 *                        1970.
 *   storage_mount()    - the FAT volume, so the game's data files can be read
 *                        and saves written. Failure is not fatal; the game
 *                        reports what it could not do.
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
    probe(4);

    console_clock_init();
    probe(5);

    storage_mount();
    probe(6);
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
    static char *const console_argv[] = { RP2040_CONSOLE_ARGV0, NULL };

    (void)argc;
    (void)argv;

    engine_start();

    return rp2040_console_game_main(1, console_argv);
}

/*
 * The game may record a pid and mix it into an RNG seed. There is one process
 * and no scheduler, so any stable non-zero value is honest.
 */
int getpid(void)
{
    return 1;
}
