#ifndef RP2040_CONSOLE_SYS_TERMIOS_H
#define RP2040_CONSOLE_SYS_TERMIOS_H

/*
 * The termios surface for a terminal on RP2040.
 *
 * newlib's <termios.h> is one line: #include <sys/termios.h>. That header is
 * not installed for bare-metal ARM, so <termios.h> on this toolchain is
 * broken rather than merely thin, and any program that tries to set terminal
 * modes cannot compile. This file is what its include is looking for.
 *
 * WHAT ACTUALLY HAPPENS TO THESE FLAGS
 *
 * There is no line discipline, so most of what a program sets here cannot
 * take effect: the USB CDC link carries bytes as the host's terminal chose to
 * send them, and the host's terminal is not ours to program. But a program
 * does three things with termios that have to work:
 *
 *   - it reads the current settings to be able to restore them later, so
 *     tcgetattr() has to report something coherent and stable;
 *   - it turns echo and canonical mode off before reading single keys, and
 *     the engine needs to know that it did - see console_getchar(), which
 *     echoes only while ECHO is set;
 *   - it asks what the screen size is, or what the erase key is, and those
 *     have real answers here.
 *
 * So tcsetattr() records what it is told rather than discarding it, and the
 * subset of the settings that this console can honour - ECHO, ECHOE, ICRNL,
 * ISTRIP - is applied by the console input path itself. Everything else is
 * remembered and has no effect, which is what "no line discipline" means
 * rather than "tcsetattr() lies about succeeding".
 *
 * The struct carries c_ispeed and c_ospeed, which System V / BSD termios has
 * and POSIX's does not. They are here because programs written against BSD
 * read the baud rate through them and there is no other way to answer them;
 * the value is a formality, since the link is USB.
 */

#include <sys/types.h>

typedef unsigned char cc_t;
typedef unsigned int tcflag_t;
typedef unsigned int speed_t;

#define NCCS 12

/*
 * Control character indices, 4.3BSD numbering.
 *
 * VMIN and VTIME alias VEOF and VEOL, as they do in BSD: in canonical mode
 * those slots mean the end-of-line characters, in raw mode they mean "how
 * many bytes" and "how long to wait". Programs that switch to raw mode write
 * through the alias, so it has to be an alias here too or their setup writes
 * into a different slot than they intend.
 */
#define VINTR   0
#define VQUIT   1
#define VERASE  2
#define VKILL   3
#define VEOF    4
#define VEOL    5
#define VMIN    VEOF
#define VTIME   VEOL
#define VSTART  7
#define VSTOP   8
#define VDSUSP  9
#define VSUSP   10
#define VLNEXT  11

/* c_iflag */
#define IGNBRK  0x0001u
#define BRKINT  0x0002u
#define IGNPAR  0x0004u
#define PARMRK  0x0008u
#define INPCK   0x0010u
#define ISTRIP  0x0020u
#define INLCR   0x0040u
#define IGNCR   0x0080u
#define ICRNL   0x0100u
#define IXON    0x0200u
#define IXOFF   0x0400u
#define IXANY   0x0800u

/* c_oflag */
#define OPOST   0x0001u
#define ONLCR   0x0002u
#define OCRNL   0x0004u
#define ONOCR   0x0008u
#define ONLRET  0x0010u
#define OFILL   0x0020u
#define NLDLY   0x0040u
#define NL0     0x0000u
#define NL1     0x0040u
#define CRDLY   0x0180u
#define CR0     0x0000u
#define CR1     0x0080u
#define CR2     0x0100u
#define CR3     0x0180u
#define TABDLY  0x0600u
#define TAB0    0x0000u
#define TAB1    0x0200u
#define TAB2    0x0400u
#define TAB3    0x0600u
#define BSDLY   0x0800u
#define BS0     0x0000u
#define BS1     0x0800u
#define FFDLY   0x1000u
#define FF0     0x0000u
#define FF1     0x1000u
#define VTDLY   0x2000u
#define VT0     0x0000u
#define VT1     0x2000u

/* c_cflag */
#define CSIZE   0x0060u
#define CS5     0x0000u
#define CS6     0x0020u
#define CS7     0x0040u
#define CS8     0x0060u
#define CSTOPB  0x0080u
#define CREAD   0x0100u
#define PARENB  0x0200u
#define PARODD  0x0400u
#define HUPCL   0x0800u
#define CLOCAL  0x1000u
#define CBAUD   0x001fu
#define B0      0x0000u
#define B50     0x0001u
#define B75     0x0002u
#define B110    0x0003u
#define B134    0x0004u
#define B150    0x0005u
#define B200    0x0006u
#define B300    0x0007u
#define B600    0x0008u
#define B1200   0x0009u
#define B1800   0x000au
#define B2400   0x000bu
#define B4800   0x000cu
#define B9600   0x000du
#define B19200  0x000eu
#define B38400  0x000fu
#define B57600  0x0010u
#define B115200 0x0011u

#ifndef _POSIX_VDISABLE
#define _POSIX_VDISABLE 0
#endif

/* c_lflag */
#define ISIG    0x0001u
#define ICANON  0x0002u
#define ECHO    0x0004u
#define ECHOE   0x0008u
#define ECHOK   0x0010u
#define ECHONL  0x0020u
#define NOFLSH  0x0040u
#define TOSTOP  0x0080u
#define ECHOCTL 0x0100u
#define ECHOKE  0x0200u
#define PENDIN  0x0400u
#define IEXTEN  0x0800u

/* tcsetattr() actions */
#define TCSANOW   0
#define TCSADRAIN 1
#define TCSAFLUSH 2

/* tcflow() actions */
#define TCOOFF 0
#define TCOON  1
#define TCIOFF 2
#define TCION  3

/* tcflush() queue selectors */
#define TCIFLUSH  0
#define TCOFLUSH  1
#define TCIOFLUSH 2

struct termios
{
    tcflag_t c_iflag;
    tcflag_t c_oflag;
    tcflag_t c_cflag;
    tcflag_t c_lflag;
    cc_t     c_cc[NCCS];
    speed_t  c_ispeed;
    speed_t  c_ospeed;
};

int tcgetattr(int fd, struct termios *t);
int tcsetattr(int fd, int actions, const struct termios *t);
int tcsendbreak(int fd, int duration);
int tcdrain(int fd);
int tcflush(int fd, int queue);
int tcflow(int fd, int action);
speed_t cfgetispeed(const struct termios *t);
speed_t cfgetospeed(const struct termios *t);
int cfsetispeed(struct termios *t, speed_t speed);
int cfsetospeed(struct termios *t, speed_t speed);
int cfsetspeed(struct termios *t, speed_t speed);

#endif /* RP2040_CONSOLE_SYS_TERMIOS_H */
