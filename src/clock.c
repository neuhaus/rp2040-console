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
 * the default, which is whenever the last session ended, or the moment this
 * program was built if there was none - recent enough to look sane in a
 * score table and to put the moon in the right place, and the board is never
 * left at the epoch because nobody was at the keyboard.
 *
 * Two details worth having in mind:
 *
 *   Input is read with a timeout. If nothing is attached - the board
 *   running from a phone charger, say - waiting for a character would hang
 *   forever, because a CDC link with no host open does not deliver an end
 *   of file either. Silence is treated as "accept the default".
 *
 *   The prompt echoes what is typed, because the host does not - screen
 *   leaves echoing to this end throughout.
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
#include <fcntl.h>
#include <unistd.h>

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
static long days_from_civil(int y, int m, int d)
{
    y -= m <= 2;
    const long era = (y >= 0 ? y : y - 399) / 400;
    const long yoe = y - era * 400;
    /*
     * The year counts from March, hence -3 after February and +9 for
     * January and February.
     */
    const long doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const long doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097L + doe - 719468L;
}

static time_t epoch_of(int y, int mo, int d, int hh, int mm, int ss)
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

    return epoch_of(year, (int)(m - months) / 3 + 1, day, hh, mm, ss);
}

/* The last session's time, kept on the volume across reboots. */
#define clock_file  ".clock"
#define clock_magic0 'C'
#define clock_magic1 'K'
#define clock_magic2 'L'
#define clock_magic3 '2'   /* int64 time; "CKL1" was int32 */

static time_t clock_recall(void)
{
    int fd;
    uint8_t raw[12];
    size_t n = 0;
    uint64_t v;
    int k;

    fd = open(clock_file, O_RDONLY);
    if (fd < 0)
        return (time_t)0;

    while (n < sizeof(raw)) {
        int r = read(fd, raw + n, sizeof(raw) - n);
        if (r <= 0)
            break;
        n += (size_t)r;
    }
    close(fd);

    if (n != sizeof(raw))
        return (time_t)0;
    if (raw[0] != clock_magic0 || raw[1] != clock_magic1 ||
        raw[2] != clock_magic2 || raw[3] != clock_magic3)
        return (time_t)0;

    v = 0;
    for (k = 0; k < 8; k++)
        v |= (uint64_t)raw[4 + k] << (8 * k);

    /*
     * Sanity, not paranoia: anything outside 1980..2107 cannot have come
     * from this clock. Bounds in unfolded epoch seconds, wide enough for
     * leap days either side.
     */
    if (v < 315532800ULL || v > 4350748800ULL)
        return (time_t)0;
    return (time_t)v;
}

void console_clock_persist(void)
{
    int fd;
    uint64_t now = (uint64_t)console_clock_now();
    uint8_t raw[12];
    size_t n = 0;
    int k;

    raw[0] = clock_magic0;
    raw[1] = clock_magic1;
    raw[2] = clock_magic2;
    raw[3] = clock_magic3;
    for (k = 0; k < 8; k++)
        raw[4 + k] = (uint8_t)(now >> (8 * k));

    fd = open(clock_file, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd < 0)
        return;
    while (n < sizeof(raw)) {
        int w = write(fd, raw + n, sizeof(raw) - n);
        if (w <= 0)
            break;
        n += (size_t)w;
    }
    close(fd);
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
        /* getchar_timeout_us takes microseconds; the limits are milliseconds. */
        int c = getchar_timeout_us(n == 0 ? RP2040_CONSOLE_CLOCK_FIRST_MS * 1000u
                                          : RP2040_CONSOLE_CLOCK_CHAR_MS * 1000u);

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

    /*
     * 2107 rather than a rounder number: FAT directory years stop there, so
     * a later date cannot be recorded for the file it would date. Refusing
     * it is kinder than granting what cannot be kept.
     */
    if (y < 1970 || y > 2107)
        return false;
    if (mo < 1 || mo > 12 || d < 1 || d > 31)
        return false;
    if (hh > 23 || mm > 59 || ss > 59)
        return false;

    *out = epoch_of(y, mo, d, hh, mm, ss);
    return true;
}

void console_clock_init(void)
{
    time_t deflt = build_epoch();
    time_t remembered = clock_recall();
    char line[40];

    /*
     * The later of the build date and whenever the last session ended. The
     * clock does not survive a reboot on its own, but the volume does, so a
     * board that ran yesterday offers yesterday instead of the build date -
     * and still offers it for correction, because the date matters to the
     * game.
     */
    if (remembered > deflt)
        deflt = remembered;

#ifdef RP2040_CONSOLE_CLOCK_NO_PROMPT
    /*
     * Built without the prompt: a game meant to run unattended, or one that
     * does not care about the date, takes the default with no delay.
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

    /*
     * Remembered straight away, so a board that loses power mid-session
     * still re-offers this session's date rather than the build's. The
     * reboot path stores it again with the elapsed time included.
     */
    console_clock_persist();
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
