/*
 * The console I/O surface.
 *
 * Two problems live here, and both come from the same thing: there is no
 * device between the program and the terminal, so the small amount of work a
 * tty used to do has to be done by somebody.
 *
 *   Input:  a program expects the line discipline to have turned Enter into a
 *           newline, echoed what it typed, and held a line back until Enter
 *           arrived - applying Backspace to what it held rather than passing
 *           it on. Nothing has done any of that here, and the SDK's own input
 *           wrapper does not either: it hands over whatever the USB driver
 *           received.
 *
 *   Output: the SDK makes libc's stdio work by --wrapping putchar(), puts(),
 *           printf() and vprintf() onto its stdio drivers. That covers most
 *           printing but not the paths that go through write(2), so bytes
 *           written to stdout or stderr by a program - fputs(), fprintf(),
 *           fwrite() - reach _write() with a descriptor of 1 or 2 and, since
 *           those are not files, are dropped with the call reporting success.
 *           fileio.c routes those two descriptors here instead.
 */

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <errno.h>
#include <sys/select.h>

#include <pico/stdio.h>
#include <pico/stdlib.h>
#include <pico/time.h>

#include "console.h"

/* Linker --wrap entry points have the same ABI as their libc originals. */
extern int __real_setvbuf(FILE *stream, char *buf, int mode, size_t size);
extern void __real_setbuf(FILE *stream, char *buf);

/*
 * One raw byte read ahead by console_input_ready() and not yet delivered.
 * The SDK's stdio has no way to look at input without taking it, so select()
 * takes it and leaves it here; every read path empties this first.
 */
static int pending_input = -1;

static int raw_getchar_timeout_us(uint32_t us)
{
    if (pending_input >= 0) {
        int c = pending_input;

        pending_input = -1;
        return c;
    }
    return stdio_getchar_timeout_us(us);
}

/*
 * The canonical input buffer.
 *
 * In canonical mode a terminal hands the program a finished line, not the
 * characters as they are typed: it holds them, applies erase and kill to what
 * it holds, and only passes the line on when Enter arrives. With no device to
 * do that, the holding has to happen here, or Backspace reaches the program as
 * a literal character and the erased text stays in the line it reads.
 *
 * While a line is being collected line_ready is false and line_len grows.
 * When a character ends the line, line_ready is set and line_buf[line_pos,
 * line_len) is handed back one character at a time.
 */
#define CONSOLE_LINE_MAX 256

static char line_buf[CONSOLE_LINE_MAX];
static size_t line_len;
static size_t line_pos;
static bool line_ready;

/* Forget whatever line was collected or being collected. */
static void line_reset(void)
{
    line_len = 0;
    line_pos = 0;
    line_ready = false;
}

void console_discard_input(void)
{
    pending_input = -1;
    line_reset();

    /* Drain existing input, bounded even if a noisy UART keeps transmitting. */
    for (unsigned i = 0; i < 1024; ++i)
        if (raw_getchar_timeout_us(0) < 0) break;
}

/*
 * The translations a line discipline performs before anything sees the
 * character. Returns the character to use, or clears *keep for one that is
 * discarded (IGNCR ignoring a carriage return) and which the caller should
 * go back to the device for another.
 */
static int translate_input(int c, int *keep)
{
    struct termios *t = console_termios();

    if (t->c_iflag & ISTRIP)
        c &= 0x7f;

    /*
     * Backspace, whichever way the host sends it.
     *
     * Terminals send DEL (0x7f) for Backspace; a great deal of older code tests
     * for BS (0x08) and compares other characters against c_cc[VERASE]. Both
     * routes have to agree or erase works in one place and not the next, so the
     * character is normalised to BS and VERASE is reported as BS too. See
     * termios_emul.c.
     */
    if (c == 0x7f)
        c = '\b';

    if (c == '\r') {
        if (t->c_iflag & IGNCR) {
            *keep = 0;
            return c;
        }

        if (t->c_iflag & ICRNL)
            c = '\n';
    } else if (c == '\n' && (t->c_iflag & INLCR)) {
        c = '\r';
    }

    return c;
}

/*
 * Echo one character, if the program has not asked us to stop echoing.
 *
 * This is the whole reason the engine keeps a termios state at all. A program
 * that reads a line - the "Who are you?" prompt, say - has never echoed: the
 * terminal did it, and the program is correct that it is not its business. A
 * program reading single keys turns ECHO off first and expects silence.
 * Honouring the flag serves both; doing neither, or always echoing, serves
 * neither.
 *
 * Control characters other than tab and newline are not echoed: ECHOCTL's
 * "^C" rendering is a nicety, and a program that receives a control character
 * is handling it as a command, not displaying it.
 */
static void echo_char(int c)
{
    struct termios *t = console_termios();

    if (!(t->c_lflag & ECHO))
        return;

    if (c == '\n' || c == '\t' || (c >= ' ' && c < 0x7f)) {
        char as_string[2] = { (char)c, '\0' };

        console_rawout(as_string);
    }
}

/* Rub out the last echoed character, the way ECHOE asks. */
static void echo_erase(void)
{
    struct termios *t = console_termios();

    if ((t->c_lflag & (ECHO | ECHOE)) == (ECHO | ECHOE))
        console_rawout("\b \b");
}

/*
 * Add one already-translated character to the line being collected, applying
 * erase and kill to what is held. Returns true when the character ended the
 * line.
 *
 * A line longer than the buffer keeps the first CONSOLE_LINE_MAX characters
 * and drops the rest; the program still receives a complete, newline-ended
 * line, which is what a terminal guarantees and more than one that discards
 * the line would.
 */
static bool canonical_feed(int c)
{
    struct termios *t = console_termios();
    const cc_t *cc = t->c_cc;

    if (c == '\b' || (cc[VERASE] != 0 && c == cc[VERASE])) {
        if (line_len > 0) {
            line_len--;
            echo_erase();
        }

        return false;
    }

    if (cc[VKILL] != 0 && c == cc[VKILL]) {
        while (line_len > 0) {
            line_len--;
            echo_erase();
        }

        return false;
    }

    if (line_len < sizeof line_buf) {
        line_buf[line_len++] = (char)c;
        echo_char(c);
    }

    return c == '\n';
}

/*
 * Read one character, as a tty would deliver it.
 *
 * stdio_getchar() is the SDK's own read across whichever stdio drivers are
 * enabled. It is called directly rather than through getchar() because
 * getchar is the very symbol being redefined, and the engine must reach the
 * version underneath.
 */
int console_getchar(void)
{
    for (;;) {
        /* Hand back a line that has already been completed. */
        if (line_ready) {
            if (line_pos < line_len)
                return (unsigned char)line_buf[line_pos++];

            line_reset();
        }

        int c = pending_input >= 0 ? raw_getchar_timeout_us(0) : stdio_getchar();

        if (c < 0)
            return c;           /* PICO_ERROR_TIMEOUT or PICO_ERROR_NO_DATA */

        int keep = 1;
        c = translate_input(c, &keep);

        if (!keep)
            continue;

        if (!(console_termios()->c_lflag & ICANON)) {
            /*
             * Raw: the program asked for characters as they are typed, so it
             * gets them as they arrive. A partial line held from a previous
             * canonical read is not part of what it just asked for.
             */
            if (line_len > 0)
                line_reset();

            if (c == '\b')
                echo_erase();
            else
                echo_char(c);

            return c;
        }

        if (canonical_feed(c)) {
            line_ready = true;
            line_pos = 0;
            return (unsigned char)line_buf[line_pos++];
        }
    }
}

/*
 * The same, but returns -1 instead of waiting when nothing has been typed.
 * Used to fill out a read() once the first byte has arrived.
 */
static int console_getchar_nowait(void)
{
    if (line_ready) {
        if (line_pos < line_len)
            return (unsigned char)line_buf[line_pos++];

        line_reset();
        return -1;
    }

    if (console_termios()->c_lflag & ICANON) {
        /*
         * Canonical mode has nothing to give until Enter completes a line,
         * and waiting for that is what the first byte of the read is for.
         */
        return -1;
    }

    /* As above: a partial canonical line is not what raw mode is reading. */
    if (line_len > 0)
        line_reset();

    for (;;) {
        int c = raw_getchar_timeout_us(0);

        if (c < 0)
            return -1;

        int keep = 1;
        c = translate_input(c, &keep);

        if (!keep)
            continue;

        if (c == '\b')
            echo_erase();
        else
            echo_char(c);

        return c;
    }
}

int console_input_ready(int64_t timeout_us)
{
    if (pending_input >= 0)
        return 1;

    int c;

    if (timeout_us < 0) {
        c = stdio_getchar();
    } else {
        absolute_time_t until = make_timeout_time_us((uint64_t)timeout_us);

        do {
            c = stdio_getchar_timeout_us(0);
        } while (c < 0 && !time_reached(until));
    }
    if (c < 0)
        return 0;
    pending_input = c;
    return 1;
}

/*
 * A terminal read: block for the first byte, then take what is already
 * waiting, stopping after a line end. Holding a caller until its full count
 * arrived would be wrong for a terminal, and a program asking for 80 bytes of
 * typed input wants the line it got.
 */
int console_read(void *buf, size_t count)
{
    unsigned char *p = buf;
    size_t n = 0;

    if (count == 0)
        return 0;

    int c = console_getchar();

    if (c < 0)
        return -1;

    p[n++] = (unsigned char)c;

    while (c != '\n' && n < count) {
        c = console_getchar_nowait();

        if (c < 0)
            break;

        p[n++] = (unsigned char)c;
    }

    return (int)n;
}

/*
 * One line of input. Returns the length, or 0 for "nothing entered" -
 * which covers Enter on an empty line, ^C, and running out of patience
 * before the first character.
 *
 * Backspace rewinds and sends "\b \b" so the host terminal rubs out its own
 * echo.
 */
size_t console_read_line(char *buf, size_t cap, unsigned first_ms,
                         unsigned char_ms)
{
    size_t n = 0;

    for (;;) {
        /* getchar_timeout_us takes microseconds; the limits are milliseconds. */
        int c = getchar_timeout_us(n == 0 ? first_ms * 1000u
                                          : char_ms * 1000u);

        if (c == PICO_ERROR_TIMEOUT || c == PICO_ERROR_NO_DATA) {
            buf[0] = 0;
            return 0;
        }
        if (c == '\r' || c == '\n') { /* the host sends CR, some send LF */
            putchar('\n');           /* the host does not echo, so this end does */
            buf[n] = 0;
            return n;
        }
        if (c == 3) { /* ^C: give up, take the default */
            buf[0] = 0;
            return 0;
        }
        if (c == 0x7f || c == '\b') {
            if (n > 0) {
                n--;
                printf("\b \b");
            }
            continue;
        }
        if (c >= ' ' && c < 127 && n + 1 < cap) {
            putchar(c);
            buf[n++] = (char)c;
        }
    }
}

/*
 * select() as a terminal program uses it: to ask whether a key is waiting,
 * typically to tell an arrow-key sequence from a lone ESC. Only the console
 * can ever be not ready; files and output are always ready, which is true of
 * a disk file on Unix as well. Exception sets are never reported.
 */
int select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
           struct timeval *timeout)
{
    int64_t wait_us = -1;
    int ready = 0;

    if (nfds < 0 || nfds > FD_SETSIZE) {
        errno = EINVAL;
        return -1;
    }
    if (timeout != NULL)
        wait_us = (int64_t)timeout->tv_sec * 1000000 + timeout->tv_usec;

    /* Everything other than console input is ready immediately. */
    for (int fd = 0; fd < nfds; fd++) {
        if (readfds && FD_ISSET(fd, readfds) && fd != 0)
            ready++;
        if (writefds && FD_ISSET(fd, writefds))
            ready++;
    }
    if (exceptfds)
        FD_ZERO(exceptfds);

    if (readfds && nfds > 0 && FD_ISSET(0, readfds)) {
        if (console_input_ready(ready ? 0 : wait_us))
            ready++;
        else
            FD_CLR(0, readfds);
    } else if (!ready && wait_us > 0) {
        sleep_us((uint64_t)wait_us);
    }
    return ready;
}

/*
 * Emit count bytes, translated exactly as putchar() would translate them -
 * one call, not one per byte, so a screen update costs one USB transaction
 * rather than a few thousand.
 */
void console_write(const void *buf, size_t count)
{
    /* One call, not one per byte: same bytes as putchar would send. */
    if (count > 0)
        stdio_put_string((const char *)buf, (int)count, false, true);
}

/*
 * Emit a string with nothing added.
 *
 * Not puts(), which appends a newline, and not anything that interprets the
 * text: callers pad and align what they pass here, so it has to leave exactly
 * as it arrived. Used for echoing, where reformatting would be visible.
 */
void console_rawout(const char *s)
{
    console_write(s, strlen(s));
}

/*
 * The buffering policy of the console streams, which is not the program's to
 * set.
 *
 * The console has two writers. putchar(), puts() and printf() are --wrapped onto
 * the SDK's stdio drivers by pico_stdio and go out immediately; fputs(),
 * fprintf() and fwrite() stay inside newlib and arrive at _write(). Both paths
 * are correct on their own. Buffered, they are not correct together: the
 * unbuffered path overtakes the buffered one and a program sees its own output
 * rearranged.
 *
 * It matters because programs do set buffering. A game's startup may well call
 * setbuf(stdout, buf) with a kilobyte buffer of its own, which is reasonable on a
 * unix where it is the only writer and would here put every fputs() and fprintf()
 * up to a kilobyte behind the printf() calls that surround them - and lose the
 * whole buffer on a reset, which is how a program that reboots on exit ends up
 * printing half a score table.
 *
 * So the request is answered for files as asked and refused for the console,
 * which is given no buffering whatever it is asked for. Returning success rather
 * than failure is not a lie: no buffering delivers output no later than any other
 * policy would, which is the only promise a program can be making here. A program
 * told that its request failed would conclude the terminal is broken.
 *
 * --wrap works for these, where it does not work for main: both are C library
 * symbols, so the reference really is one that would be resolved out of an
 * archive, which is the case --wrap was written for. See the note in
 * cmake/rp2040-console.cmake.
 */
int __wrap_setvbuf(FILE *stream, char *buf, int mode, size_t size)
{
    if (stream == stdout || stream == stderr)
        return __real_setvbuf(stream, NULL, _IONBF, 0);

    return __real_setvbuf(stream, buf, mode, size);
}

void __wrap_setbuf(FILE *stream, char *buf)
{
    if (stream == stdout || stream == stderr) {
        (void)__real_setvbuf(stream, NULL, _IONBF, 0);
        return;
    }

    __real_setbuf(stream, buf);
}

/*
 * Wait for a host with the port open, before the first thing printed.
 *
 * A terminal opened a second after power-on would otherwise miss the
 * questions asked at startup and time out answering what it never saw -
 * looking exactly like a board that does not ask. Zero waits until someone
 * is listening; a game meant to run unattended sets a timeout instead.
 *
 * This watches DTR itself rather than stdio_usb_connected(): output here
 * does not need DTR, but without it there is nobody to read it. DTR is what
 * a terminal opening the port asserts. tud_cdc_connected() is a macro, so
 * only the header reaches it.
 */
#if LIB_PICO_STDIO_USB
#include "tusb.h"
#endif

void console_wait_for_terminal(int ms)
{
#if LIB_PICO_STDIO_USB
    /* Zero waits until someone is listening; see above. */
    absolute_time_t limit = ms > 0 ? make_timeout_time_ms((uint32_t)ms)
                                   : at_the_end_of_time;

    while (!tud_cdc_connected() && !time_reached(limit))
        sleep_ms(10);

    /*
     * A prompt written the instant DTR rises can be swallowed by the terminal
     * installing its line settings; the SDK waits after connecting too.
     */
    if (tud_cdc_connected())
        sleep_ms(50);
#else
    (void) ms;
#endif
}

/*
 * Optional UART input, built with -DRP2040_CONSOLE_UART_STDIN=ON.
 *
 * Input arrives through the stdio drivers, and with only USB CDC attached
 * that works only while a host holds the CDC port open. A UART bridge is how
 * to talk to a board that will not enumerate, or when no host USB stack is
 * willing to.
 *
 * Off by default: a second input driver is also how a noisy or disconnected
 * cable becomes an endless stream of commands, so it is opt-in rather than
 * always-on insurance.
 *
 * Input only. Output stays on USB alone, so nothing is printed twice.
 */
#ifdef RP2040_CONSOLE_UART_STDIN


#include <hardware/gpio.h>
#include <hardware/uart.h>
#include <boards/pico.h>
#include <pico/stdio/driver.h>
#include <pico/stdio_uart.h>

/*
 * Bits 8..11 of a UART data-register read are error flags, not data: OE
 * (overrun), BE (break), PE (parity), FE (framing). The byte is bits 7..0.
 */
#define UART_DR_DATA_MASK   0xFFu
#define UART_DR_ERROR_MASK  0xF00u

static int console_uart_getchars(char *buf, int len)
{
    uart_inst_t *u = uart_get_instance(0);
    int n = 0;

    while (n < len && uart_is_readable(u)) {
        /*
         * Discard anything the UART flagged as bad.
         *
         * This is not defensive decoration. A GPIO left floating, or one held
         * low by a short or an unpowered bridge, makes the receiver see a
         * permanent break or framing condition and deliver bytes at the baud
         * rate forever. A program reading stdin then consumes that noise as
         * input as fast as it arrives.
         *
         * A byte that arrived with an error flag set is line noise, not
         * something anybody typed.
         */
        uint32_t word = uart_get_hw(u)->dr;

        if (word & UART_DR_ERROR_MASK)
            continue;

        buf[n++] = (char)(word & UART_DR_DATA_MASK);
    }

    return n;
}

/* Input-only driver: output stays with the SDK's USB stdio. */
static stdio_driver_t console_uart_driver = {
    .out_chars = NULL,
    .out_flush = NULL,
    .in_chars = console_uart_getchars,
    .set_chars_available_callback = NULL,
};

void console_uart_stdin_init(void)
{
    uart_inst_t *u = uart_get_instance(0);

    /*
     * Hold RX high when nothing drives it, so a disconnected cable reads as an
     * idle line rather than as noise. PICO_DEFAULT_UART_RX_PIN is the board's
     * own UART0 RX pin rather than a number that would be wrong on any other
     * board.
     */
    gpio_set_pulls(PICO_DEFAULT_UART_RX_PIN, true, false);

    /* Throw away whatever the line produced during startup. */
    while (uart_is_readable(u))
        (void)uart_get_hw(u)->dr;

    stdio_set_driver_enabled(&console_uart_driver, true);
}

#endif /* RP2040_CONSOLE_UART_STDIN */
