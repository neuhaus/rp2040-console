/*
 * Startup diagnostics for the engine.
 *
 * From the host, "the firmware is not running" and "the firmware is
 * running but USB never enumerated" are one and the same observation: no
 * serial port. These probes report progress over UART0, which works
 * regardless of what USB is doing, so the two cases can be told apart
 * without a debugger.
 *
 * Each stage writes a single identifiable line so the UART log shows
 * exactly how far startup got:
 *
 *   [1] CRT reached C runtime        - board and flash are fine
 *   [2] reached the wrapped main()   - .data/.bss init and ctors OK
 *   [3] stdio_init_all() returned    - USB + UART drivers initialised
 *   [4] env_init() done              - TERM and friends available
 *   [5] console_clock_init() done    - date settled, prompt not blocking
 *   [6] storage_mount() done         - volume mounted, or absent
 *
 * A game that stops before [6] has a different problem than one that stops
 * after it: the first five are engine and hardware, everything past six is
 * the program's own startup.
 *
 * UART0 only (GPIO0 TX, GPIO1 RX, 115200). Deliberately does not use
 * stdio, because stdio may be exactly what is broken.
 */

#include <pico/stdlib.h>
#include <hardware/uart.h>

#include "probes.h"

bool probe_enabled = false;

static void probe_raw(const char *s)
{
    uart_inst_t *u = uart_get_instance(0);

    for (const char *p = s; *p != '\0'; p++)
        uart_putc(u, *p);
}

void probe_init(void)
{
    /*
     * Enable UART0 immediately. uart_init also installs the pins, so
     * this is the first thing that must work for any output at all.
     */
    uart_init(uart_get_instance(0), 115200);
    uart_set_format(uart_get_instance(0), 8, 1, 0);
    uart_set_translate_crlf(uart_get_instance(0), true);

    probe_enabled = true;
    probe_raw("\r\n[b] c runtime\r\n");
}

void probe(int stage)
{
    if (!probe_enabled)
        return;

    static const char digits[] = "0123456789abcdef";

    char buf[16];
    int n = 0;

    buf[n++] = '[';
    buf[n++] = stage <= 9 ? '0' + stage : 'a' + (stage - 10);
    buf[n++] = ']';
    buf[n++] = ' ';
    buf[n] = '\0';

    (void)digits;
    probe_raw(buf);
}