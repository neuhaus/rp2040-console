/*
 * Host stand-in for <pico/time.h>, for the console input tests only.
 *
 * src/console.c references the timeout type and clocks from code the test does
 * not call - the poll loop in select() - so the type and the few declarations
 * it needs are here, and the test supplies the definitions.
 */
#ifndef RP2040_CONSOLE_TEST_PICO_TIME_H
#define RP2040_CONSOLE_TEST_PICO_TIME_H

#include <stdbool.h>
#include <stdint.h>

/* As the SDK defines it: a timestamp in microseconds since boot. */
typedef struct {
    int64_t _private_us_since_boot;
} absolute_time_t;

absolute_time_t make_timeout_time_us(uint64_t us);
bool time_reached(absolute_time_t t);
void sleep_us(uint64_t us);
void sleep_ms(uint32_t ms);

#endif /* RP2040_CONSOLE_TEST_PICO_TIME_H */
