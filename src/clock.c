/*
 * A date and time to run under.
 *
 * A Pico has no clock that outlives its power. Nothing on the board knows
 * the year, and the usual fallbacks a hosted program has - a system clock,
 * an RTC, a network - are exactly the things that are missing here. A game
 * that stamps score files, tombstones or a moon phase therefore has to be
 * told, or it will report 1970 forever.
 *
 * So the engine asks, once, before the game starts. Pressing Enter accepts
 * the default, which is the moment this program was built - not current,
 * but recent enough to look sane in a score table and to put the moon in
 * the right place, and it means the board is never left at the epoch
 * because nobody was at the keyboard.
 *
 * Two details worth having in mind:
 *
 *   Input is read with a timeout. If nothing is attached - the board
 *   running from a phone charger, say - waiting for a character would hang
 *   forever, because a CDC link with no host open does not deliver an end
 *   of file either. Silence is treated as "accept the default".
 *
 *   Typed characters are not echoed from here. Echo is the host terminal's
 *   job throughout this port - it is how the game itself receives input -
 *   and echoing here as well would double them on screen.
 *
 * The value entered is taken as UTC. newlib's localtime() with no timezone
 * set is gmtime(), so what the player types is what the game later reads
 * back.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#include <pico/stdlib.h>

#include "clock.h"

/* Wait this long for the first character before giving up on input. */
#ifndef RP2040_CONSOLE_CLOCK_FIRST_MS
#define RP2040_CONSOLE_CLOCK_FIRST_MS 15000u
#endif

/* And this long between characters, for someone typing slowly. */
#ifndef RP2040_CONSOLE_CLOCK_CHAR_MS
#define RP2040_CONSOLE_CLOCK_CHAR_MS 10000u
#endif

static time_t clock_base;       /* epoch seconds, as of clock_boot_us */
static uint64_t clock_boot_us;  /* time_us_64() when that base was chosen */

/*
 * Days from 1970-01-01 for a civil date, including the leap-year rules.
 * Howard Hinnant's days_from_civil: no table, no loop, correct for the
 * whole range a player might type in.
 */
static long days_from_civil(int y, unsigned m, unsigned d)
{
    y -= m <= 2;
    const long era = (y >= 0 ? y : y - 399) / 400;
    const unsigned yoe = (unsigned)(y - era * 400);
    const unsigned doy = (153u * (m + (m > 2 ? 3u : 9u)) + 2u) / 5u + d - 1u;
    const unsigned doe = yoe * 365u + yoe / 4u - yoe / 100u + doy;
    return era * 146097L + (long)doe - 719468L;
}

static time_t epoch_of(int y, unsigned mo, unsigned d,
                       int hh, int mm, int ss)
{
    return (time_t)(days_from_civil(y, mo, d) * 86400L +
                    hh * 3600 + mm * 60 + ss);
}

/* Render an epoch as the same shape the prompt asks for. */
static const char *display(time_t t)
{
    static char buf[32];
    struct tm *tm = gmtime(&t);

    if (tm == NULL)
        return "1970-01-01 00:00";

    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", tm);
    return buf;
}

/*
 * The build timestamp, used when the player enters nothing. __DATE__ is
 * "Oct 11 2026" and __TIME__ "14:03:59"; a build with source-date-epoch or
 * a non-standard format yields 0, which is harmless - the player can still
 * type a date.
 */
static time_t build_epoch(void)
{
    static const char months[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    char mon[4] = { 0, 0, 0, 0 };
    int day = 1, year = 1970, hh = 0, mm = 0, ss = 0;

    if (sscanf(__DATE__, "%3s %d %d", mon, &day, &year) != 3)
        return 0;

    const char *m = strstr(months, mon);
    if (m == NULL)
        return 0;

    sscanf(__TIME__, "%d:%d:%d", &hh, &mm, &ss);

    return epoch_of(year, (unsigned)((m - months) / 3) + 1u, (unsigned)day,
                    hh, mm, ss);
}

/*
 * One line of input. Returns the length, or 0 for "nothing entered" -
 * which covers Enter on an empty line, ^C, and running out of patience
 * before the first character.
 *
 * Backspace rewinds and sends "\b \b" so the host terminal rubs out its own
 * echo.
 */
static size_t read_line(char *buf, size_t cap)
{
    size_t n = 0;

    for (;;) {
        int c = getchar_timeout_us(n == 0 ? RP2040_CONSOLE_CLOCK_FIRST_MS
                                          : RP2040_CONSOLE_CLOCK_CHAR_MS);

        if (c == PICO_ERROR_TIMEOUT || c == PICO_ERROR_NO_DATA) {
            buf[0] = 0;
            return 0;
        }
        if (c == '\r' || c == '\n') { /* the host sends CR, some send LF */
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
        if (c >= ' ' && c < 127 && n + 1 < cap)
            buf[n++] = (char)c;
    }
}

/*
 * "YYYY-MM-DD", "YYYY-MM-DD HH:MM" and "YYYY-MM-DD HH:MM:SS", with '/'
 * accepted instead of '-'. Anything else is refused rather than guessed
 * at: a date that is quietly wrong is worse than one the player has to
 * repeat.
 */
static bool parse_datetime(const char *s, time_t *out)
{
    int y = 0, mo = 0, d = 0, hh = 0, mm = 0, ss = 0;

    while (*s == ' ')
        s++;
    if (*s == 0)
        return false;

    int fields = sscanf(s, "%d-%d-%d %d:%d:%d", &y, &mo, &d, &hh, &mm, &ss);
    if (fields < 3) {
        y = mo = d = hh = mm = ss = 0;
        fields = sscanf(s, "%d/%d/%d %d:%d:%d", &y, &mo, &d, &hh, &mm, &ss);
    }
    if (fields < 3)
        return false;

    if (y < 1970 || y > 2099)
        return false;
    if (mo < 1 || mo > 12 || d < 1 || d > 31)
        return false;
    if (hh > 23 || mm > 59 || ss > 59)
        return false;

    *out = epoch_of(y, (unsigned)mo, (unsigned)d, hh, mm, ss);
    return true;
}

void console_clock_init(void)
{
    time_t deflt = build_epoch();
    char line[40];

#ifdef RP2040_CONSOLE_CLOCK_NO_PROMPT
    /*
     * Built without the prompt: a game meant to run unattended, or one that
     * does not care about the date, gets the build epoch and no delay at
     * startup.
     */
    clock_base = deflt;
    (void)line;
#else
    printf("\nSet date and time [YYYY-MM-DD HH:MM], or Enter for %s: ",
           display(deflt));

    for (int tries = 0; tries < 3; tries++) {
        if (read_line(line, sizeof(line)) == 0) {
            clock_base = deflt;
            break;
        }
        if (parse_datetime(line, &clock_base))
            break;

        clock_base = 0;
        printf("Not understood. Type YYYY-MM-DD HH:MM, "
               "or Enter for %s: ", display(deflt));
    }

    if (clock_base == 0) {
        /* Three attempts, or every line timed out. */
        clock_base = deflt;
    }

    printf("Date set to %s\n", display(clock_base));
#endif

    /*
     * Measured after the prompt, so the seconds spent typing are not also
     * counted as time having passed since the date was set.
     */
    clock_boot_us = time_us_64();
}

time_t console_clock_now(void)
{
    /*
     * time_us_64() counts microseconds since the timer was started and is
     * 64 bits wide, so unlike to_ms_since_boot() it does not wrap on a board
     * left running for weeks. The elapsed span is measured from the moment
     * the clock was set, so anything spent in the prompt above is not also
     * charged to the clock.
     */
    return clock_base + (time_t)((time_us_64() - clock_boot_us) / 1000000ull);
}
