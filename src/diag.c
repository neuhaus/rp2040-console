/*
 * Boot progress on an onboard LED.
 *
 * A board with no display tells you nothing when it stops early, while USB
 * stays enumerated whatever the main thread is doing - so a terminal sees a
 * port that is open and never answers. The LED cannot lie about whether the
 * program is running.
 *
 * Every two seconds the LED pulses once for each boot stage reached: blinking
 * twice, pausing, twice means the boot is stuck at stage 2. The pulse comes
 * from an alarm interrupt, so it continues while the processor takes
 * interrupts at all.
 *
 * The LED pin cannot be discovered at run time, so RP2040_CONSOLE_LED is a
 * comma separated list, pulsed together - 25 on a Raspberry Pi Pico, 24 on a
 * Waveshare RP2040-Zero, 22 on a Pimoroni Pico Plus 2. Empty disables the
 * reporter: no pins claimed, no timer running.
 */

#include "hardware/gpio.h"
#include "pico/time.h"

#include "diag.h"

#ifndef RP2040_CONSOLE_LED
#define RP2040_CONSOLE_LED "25"
#endif

#define max_leds 4

static volatile int stage;
static int leds[max_leds];
static int n_leds;

void console_diag_stage(int s)
{
    if (s > stage)
        stage = s;
}

static bool report(repeating_timer_t *rt)
{
    /*
     * Interrupt context, so no waiting: a 100ms tick steps through a
     * two-second pattern, one 200ms pulse per stage reached.
     */
    static int tick;
    int pos, on;

    (void) rt;
    pos = tick % 20;
    tick++;

    on = (pos < stage * 2) && ((pos & 1) == 0);
    for (int j = 0; j < n_leds; ++j)
        gpio_put(leds[j], on);
    return true;                     /* keep repeating */
}

void console_diag_init(void)
{
    static repeating_timer_t timer;
    const char *p = RP2040_CONSOLE_LED;

    while (*p != '\0' && n_leds < max_leds) {
        if (*p >= '0' && *p <= '9') {
            int pin = 0;
            while (*p >= '0' && *p <= '9')
                pin = pin * 10 + (*p++ - '0');
            if (pin <= 29) {         /* RP2040 has GPIO0..GPIO29 */
                gpio_init(pin);
                gpio_set_dir(pin, GPIO_OUT);
                gpio_put(pin, 0);
                leds[n_leds++] = pin;
            }
        }
        while (*p != '\0' && *p != ',')
            p++;                     /* skip anything unparsable */
        if (*p == ',')
            p++;
    }

    console_diag_stage(DIAG_STAGE_STARTED);
    if (n_leds > 0)
        add_repeating_timer_ms(-100, report, NULL, &timer);
}
