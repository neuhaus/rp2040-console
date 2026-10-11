#ifndef RP2040_CONSOLE_SYS_IOCTL_H
#define RP2040_CONSOLE_SYS_IOCTL_H

/*
 * ioctl() for a console with no kernel.
 *
 * This header is missing entirely from newlib for bare-metal ARM, which
 * stops a program from compiling when it only wanted to ask the terminal how
 * big it is. The request is answered here, because unlike most of ioctl it
 * has a true answer: the engine knows the size of the screen it drives.
 *
 * Everything else - the BSD job-control and line-discipline requests a
 * program uses to save and restore terminal state, of which TIOCGLTC and
 * friends are the common ones in older code - has no answer, and
 * ioctl() returns -1 with ENOTTY for it. That is the behaviour a program
 * should already cope with: those calls fail on any descriptor that is not a
 * pty, and code that saves terminal state is expected to carry on without it.
 *
 * Note the prototype. newlib does not declare ioctl() anywhere, so this
 * header has to, and the signature is the BSD one.
 */

struct winsize
{
    unsigned short ws_row;
    unsigned short ws_col;
    unsigned short ws_xpixel;   /* unused, zero */
    unsigned short ws_ypixel;   /* unused, zero */
};

#define TIOCGWINSZ 0x40087768u  /* get window size          */
#define TIOCSWINSZ 0x80087767u  /* set window size (noted)  */

int ioctl(int fd, unsigned long request, ...);

#endif /* RP2040_CONSOLE_SYS_IOCTL_H */
