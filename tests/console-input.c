/*
 * Host tests for the console input path's line discipline.
 *
 * src/console.c is built against a scripted device: the three stdio entry
 * points it calls are supplied here, output is captured, and input is a fixed
 * byte sequence. What is under test is what a program receives, and what the
 * user sees echoed, for a given termios state.
 *
 * The case that prompted this is the first one: "look", four erases,
 * "examine" has to reach the program as "examine", not "lookexamine".
 */
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>

#include <pico/stdio.h>
#include <pico/time.h>

#include "console.h"

/* --- the scripted terminal ---------------------------------------------- */

static const unsigned char *input;
static size_t input_len;
static size_t input_pos;

static char out[1024];
static size_t out_len;

static void set_input(const char *s)
{
    /* Clear any line a previous case left held in the console. */
    console_discard_input();

    input = (const unsigned char *)s;
    input_len = strlen(s);
    input_pos = 0;
    out_len = 0;
}

/* The device, as console.c reaches it through the SDK. */
int stdio_getchar(void)
{
    if (input_pos >= input_len)
        return -1;

    return input[input_pos++];
}

int stdio_getchar_timeout_us(uint32_t timeout_us)
{
    (void)timeout_us;
    return stdio_getchar();
}

void stdio_put_string(const char *s, int len, bool newline, bool flush)
{
    (void)newline;
    (void)flush;
    assert(len >= 0 && out_len + (size_t)len <= sizeof out);
    memcpy(out + out_len, s, (size_t)len);
    out_len += (size_t)len;
}

/*
 * The clocks console.c references from code the test does not call - the poll
 * loop in select(). Nothing here waits: a timeout is reached at once and
 * sleeping does nothing.
 */
absolute_time_t make_timeout_time_us(uint64_t us)
{
    absolute_time_t t = { (int64_t)us };
    return t;
}

bool time_reached(absolute_time_t t)
{
    (void)t;
    return true;
}

void sleep_us(uint64_t us)
{
    (void)us;
}

void sleep_ms(uint32_t ms)
{
    (void)ms;
}

/*
 * console.c defines __wrap_setvbuf()/__wrap_setbuf() and calls the originals.
 * A host link is not --wrapped, so the originals are supplied here.
 */
int __real_setvbuf(FILE *stream, char *buf, int mode, size_t size)
{
    (void)stream;
    (void)buf;
    (void)mode;
    (void)size;
    return 0;
}

void __real_setbuf(FILE *stream, char *buf)
{
    (void)stream;
    (void)buf;
}

/* --- helpers ------------------------------------------------------------- */

/* Run one canonical line through read() and check both the line a program
 * got and the echo a user saw. */
static void expect_line(const char *typed, const char *expect, const char *expect_echo)
{
    char buf[128];
    int n;

    set_input(typed);
    n = console_read(buf, sizeof buf);
    assert(n == (int)strlen(expect));
    assert(memcmp(buf, expect, (size_t)n) == 0);
    assert(out_len == strlen(expect_echo));
    assert(memcmp(out, expect_echo, out_len) == 0);
}

static void set_raw(int echo)
{
    struct termios t;

    assert(tcgetattr(0, &t) == 0);
    t.c_lflag &= ~ICANON;
    if (echo)
        t.c_lflag |= ECHO;
    else
        t.c_lflag &= ~ECHO;
    assert(tcsetattr(0, TCSANOW, &t) == 0);
}

int main(void)
{
    char buf[128];
    int n;

    /* --- canonical mode: the reported bug --------------------------------- */

    console_reset_terminal();

    /* "look", four erases, "examine": the erased text is gone. */
    expect_line("look\b\b\b\bexamine\n", "examine\n",
                "look\b \b\b \b\b \b\b \bexamine\n");

    /* One erase removes one character, not the whole line. */
    expect_line("ab\bc\n", "ac\n", "ab\b \bc\n");

    /* The host's DEL (0x7f) erases as well as BS. */
    expect_line("xy\x7fz\n", "xz\n", "xy\b \bz\n");

    /* Erase with nothing before it is ignored, and echoes nothing. */
    expect_line("\b\ba\n", "a\n", "a\n");

    /* Kill (^U, VKILL) clears what is held. */
    expect_line("abc\x15z\n", "z\n", "abc\b \b\b \b\b \bz\n");

    /* Enter arrives as CR; ICRNL turns it into the newline a program reads. */
    expect_line("hi\r", "hi\n", "hi\n");

    /* A newline arrives as typed. */
    expect_line("ok\n", "ok\n", "ok\n");

    /* A line longer than the buffer still ends with the newline it had. */
    {
        char big[300];
        char big_expect[300];

        memset(big, 'x', sizeof big - 2);
        big[sizeof big - 2] = '\n';
        big[sizeof big - 1] = '\0';
        memset(big_expect, 'x', sizeof big_expect - 2);
        big_expect[sizeof big_expect - 2] = '\n';
        big_expect[sizeof big_expect - 1] = '\0';

        set_input(big);
        n = console_read(buf, sizeof buf);
        assert(n == (int)sizeof buf);       /* as much as was asked for */
        assert(memcmp(buf, big_expect, sizeof buf) == 0);
    }

    /*
     * Each read() stops at a line end even when the next line is already
     * there, so a game gets one command per prompt.
     */
    set_input("a\nb\n");
    n = console_read(buf, sizeof buf);
    assert(n == 2 && memcmp(buf, "a\n", 2) == 0);
    n = console_read(buf, sizeof buf);
    assert(n == 2 && memcmp(buf, "b\n", 2) == 0);

    /* getchar() hands a completed line back one character at a time. */
    set_input("go\n");
    assert(console_getchar() == 'g');
    assert(console_getchar() == 'o');
    assert(console_getchar() == '\n');

    /* --- raw mode: the program asked for characters, it gets characters --- */

    set_raw(0);
    set_input("x");
    assert(console_getchar() == 'x');
    assert(console_getchar() == -1);
    assert(out_len == 0);                   /* ECHO off means silence */

    set_raw(1);
    set_input("q\x7fr");                     /* q, DEL, r */
    assert(console_getchar() == 'q');
    assert(console_getchar() == '\b');       /* no editing: BS is delivered */
    assert(console_getchar() == 'r');
    assert(out_len == 5 && memcmp(out, "q\b \br", 5) == 0);

    /* Raw read() returns what is waiting without waiting for a line. */
    set_raw(0);
    set_input("tz");
    n = console_read(buf, sizeof buf);
    assert(n == 2 && memcmp(buf, "tz", 2) == 0);

    return 0;
}
