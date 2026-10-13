/*
 * The terminal state a program is allowed to think it controls.
 *
 * There is no line discipline on this hardware, so terminal modes cannot be
 * enforced in the place they are normally enforced. Discarding them is not
 * the answer either: a program reads its settings at startup in order to
 * restore them later, decides from the erase character what Backspace is, and
 * turns echo off before reading single keys. If tcsetattr() throws the modes
 * away, the last of those stops working - the program asks for no echo and
 * goes on being echoed.
 *
 * So the modes are kept, and the console input path consults them. That makes
 * the small set this console can honour - ECHO, ECHOE, ICRNL, IGNCR, INLCR,
 * ISTRIP - actually take effect, and everything else is remembered and has no
 * consequence, which is an honest description of "no line discipline" rather
 * than a claim that setting a mode did something.
 */

#include <errno.h>
#include <stdarg.h>
#include <string.h>
#include <termios.h>

#include <sys/ioctl.h>

#include "console.h"

/*
 * What the console reports about itself before a program has changed it.
 *
 * A cooked terminal with echo on, because that is what a program expects to
 * find when it starts and what it saves in order to put back on exit. A
 * program that wants raw input turns these off itself, and that is the
 * transition the engine has to get right.
 *
 * VERASE is BS rather than DEL. The host's keyboard may send either; the
 * console input path normalises them to BS, and the game keeps a copy of this
 * value to compare typed characters against - so the two must agree or erase
 * works in one place and not the next. See console.c.
 *
 * The baud rate is a formality: the link is USB. It is reported because
 * programs ask, and because termcap's padding calculations index a table by
 * it, which wants the small BSD code rather than a number of bits per second.
 */
static const struct termios default_tio = {
    /* c_iflag */ ICRNL | IXON,
    /* c_oflag */ OPOST | ONLCR,
    /* c_cflag */ B9600 | CS8 | CREAD | CLOCAL,
    /* c_lflag */ ISIG | ICANON | ECHO | ECHOE | ECHOK | IEXTEN,
    /* c_cc    */ { 3, 28, '\b', 21, 4, 0, 1, 17, 19, 0, 26, 0 },
    /* c_ispeed */ B9600,
    /* c_ospeed */ B9600,
};

static struct termios console_tio;
static int terminal_initialized;

void console_reset_terminal(void)
{
    console_tio = default_tio;
    terminal_initialized = 1;
}

static void ensure_terminal(void)
{
    if (!terminal_initialized) console_reset_terminal();
}

struct termios *console_termios(void)
{
    ensure_terminal();
    return &console_tio;
}

speed_t cfgetispeed(const struct termios *t)
{
    if (t == NULL) { errno = EFAULT; return 0; }
    return t->c_ispeed;
}

speed_t cfgetospeed(const struct termios *t)
{
    if (t == NULL) { errno = EFAULT; return 0; }
    return t->c_ospeed;
}

static int valid_speed(const struct termios *t, speed_t speed)
{
    if (t == NULL) { errno = EFAULT; return 0; }
    if (speed > B115200) { errno = EINVAL; return 0; }
    return 1;
}

int cfsetispeed(struct termios *t, speed_t speed)
{
    if (!valid_speed(t, speed)) return -1;
    t->c_ispeed = speed == B0 ? t->c_ospeed : speed;
    return 0;
}

int cfsetospeed(struct termios *t, speed_t speed)
{
    if (!valid_speed(t, speed)) return -1;
    t->c_ospeed = speed;
    t->c_cflag = (t->c_cflag & ~CBAUD) | speed;
    return 0;
}

int cfsetspeed(struct termios *t, speed_t speed)
{
    if (cfsetospeed(t, speed) != 0) return -1;
    t->c_ispeed = speed;
    return 0;
}

int tcgetattr(int fd, struct termios *t)
{
    ensure_terminal();
    (void)fd;

    if (t == NULL) {
        errno = EFAULT;
        return -1;
    }

    *t = console_tio;
    return 0;
}

int tcsetattr(int fd, int actions, const struct termios *t)
{
    ensure_terminal();
    (void)fd;
    (void)actions;        /* nothing is queued, so there is nothing to drain */

    if (t == NULL) {
        errno = EFAULT;
        return -1;
    }

    console_tio = *t;
    return 0;
}

/*
 * The rest of the termios surface.
 *
 * Nothing is buffered on the way out and there is no flow control to suspend,
 * so these have nothing to do - but they are not errors, and a program that
 * checks the return value should keep going.
 */
int tcsendbreak(int fd, int duration)
{
    (void)fd;
    (void)duration;
    return 0;
}

int tcdrain(int fd)
{
    (void)fd;
    return 0;
}

int tcflush(int fd, int queue)
{
    (void)fd;
    (void)queue;
    return 0;
}

int tcflow(int fd, int action)
{
    (void)fd;
    (void)action;
    return 0;
}

/*
 * ioctl() on the console.
 *
 * One request has a true answer, and it is the one a terminal program
 * actually wants: how big is the screen. Everything else in the BSD set
 * concerns a line discipline that does not exist here, and gets -1/ENOTTY -
 * the same answer a program gets from any descriptor that is not a pty, and
 * one that code saving terminal state is written to tolerate.
 *
 * TIOCSWINSZ is accepted rather than refused. The size belongs to whatever is
 * on the other end of the cable, and a program setting it cannot be
 * contradicted usefully; refusing would be reported as a broken terminal.
 */
int ioctl(int fd, unsigned long request, ...)
{
    va_list ap;
    void *arg;

    (void)fd;

    va_start(ap, request);
    arg = va_arg(ap, void *);
    va_end(ap);

    if (request == TIOCGWINSZ) {
        if (arg == NULL) {
            errno = EFAULT;
            return -1;
        }

        struct winsize *ws = arg;

        ws->ws_row = RP2040_CONSOLE_ROWS;
        ws->ws_col = RP2040_CONSOLE_COLS;
        ws->ws_xpixel = 0;
        ws->ws_ypixel = 0;
        return 0;
    }

    if (request == TIOCSWINSZ)
        return 0;

    errno = ENOTTY;
    return -1;
}
