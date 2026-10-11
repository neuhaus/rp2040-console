/*
 * The console I/O surface.
 *
 * Two problems live here, and both come from the same thing: there is no
 * device between the program and the terminal, so the small amount of work a
 * tty used to do has to be done by somebody.
 *
 *   Input:  a program expects the line discipline to have turned Enter into a
 *           newline and to have echoed what it typed. Nothing has done that
 *           here, and the SDK's own input wrapper does not either - it hands
 *           over whatever the USB driver received.
 *
 *   Output: the SDK makes libc's stdio work by --wrapping putchar(), puts(),
 *           printf() and vprintf() onto its stdio drivers. That covers most
 *           printing but not the paths that go through write(2), so bytes
 *           written to stdout or stderr by a program - fputs(), fprintf(),
 *           fwrite() - reach _write() with a descriptor of 1 or 2 and, since
 *           those are not files, are dropped with the call reporting success.
 *           fileio.c routes those two descriptors here instead.
 */

#include <stdio.h>
#include <string.h>
#include <termios.h>

#include <pico/stdio.h>

#include "console.h"

/*
 * The input processing a line discipline used to do, in one place.
 *
 * Returns the character the caller should see, or sets *keep to 0 for a
 * character that is not deliverable (IGNCR discarding a carriage return) and
 * which the caller should go back to the device for another.
 */
static int process_input(int c, int *keep)
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

    if (c == '\b') {
        if ((t->c_lflag & (ECHO | ECHOE)) == (ECHO | ECHOE))
            console_rawout("\b \b");

        return c;
    }

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

    /*
     * Echo what the program has not asked us to stop echoing.
     *
     * This is the whole reason the engine keeps a termios state at all. A
     * program that reads a line - the "Who are you?" prompt, say - has never
     * echoed: the terminal did it, and the program is correct that it is not
     * its business. A program reading single keys turns ECHO off first and
     * expects silence. Honouring the flag serves both; doing neither, or always
     * echoing, serves neither.
     *
     * Control characters other than tab and newline are not echoed: ECHOCTL's
     * "^C" rendering is a nicety, and a program that receives a control
     * character is handling it as a command, not displaying it.
     */
    if (t->c_lflag & ECHO) {
        if (c == '\n' || c == '\t' || (c >= ' ' && c < 0x7f)) {
            char as_string[2] = { (char)c, '\0' };

            console_rawout(as_string);
        }
    }

    return c;
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
        int c = stdio_getchar();

        if (c < 0)
            return c;           /* PICO_ERROR_TIMEOUT or PICO_ERROR_NO_DATA */

        int keep = 1;
        c = process_input(c, &keep);

        if (keep)
            return c;
    }
}

/*
 * The same, but returns -1 instead of waiting when nothing has been typed.
 * Used to fill out a read() once the first byte has arrived.
 */
static int console_getchar_nowait(void)
{
    for (;;) {
        int c = stdio_getchar_timeout_us(0);

        if (c < 0)
            return -1;

        int keep = 1;
        c = process_input(c, &keep);

        if (keep)
            return c;
    }
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
 * Emit count bytes exactly as they came.
 *
 * No newline appended, no CR inserted, nothing interpreted. Output processing
 * is deliberately not applied to the newline: the terminal on the other end of
 * this link treats a bare LF as the end of a line, the SDK's own putchar path
 * has always sent it that way, and adding CR here would put two line endings on
 * everything that goes through printf().
 */
void console_write(const void *buf, size_t count)
{
    const unsigned char *p = buf;

    for (size_t i = 0; i < count; i++)
        putchar((int)p[i]);
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
 * Print the memory map over the console.
 *
 * Useful for confirming a program still fits after a change. The numbers worth
 * watching are the RAM ones: text and constant tables live in flash and cost
 * nothing there, so a program that grows 40KB and still runs has not
 * necessarily got any nearer to the limit.
 */
void report_memory(void)
{
    extern char __etext, __data_start__, __bss_start__, __bss_end__;
    extern char __StackTop;

    unsigned flash_used = (unsigned)(&__etext - &__data_start__);
    unsigned ram_static = (unsigned)(&__bss_end__ - &__data_start__);
    unsigned free_ram = (unsigned)((char *)&__StackTop - &__bss_end__);

    printf("\n=== memory ===\n");
    printf("flash text : %u (%uKB)\n", flash_used, flash_used / 1024);
    printf("RAM static : %u (%uKB)\n", ram_static, ram_static / 1024);
    printf("RAM free   : %u (%uKB)\n", free_ram, free_ram / 1024);
    printf("============\n");
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
