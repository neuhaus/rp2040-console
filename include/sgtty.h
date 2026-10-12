#ifndef _SGTTY_H_
#define _SGTTY_H_

/*
 * The BSD sgtty interface, or as much of it as a program needs in order to
 * keep compiling.
 *
 * sgtty is what terminals were driven through before termios: struct sgttyb
 * for the modes, TIOCGETP/TIOCSETP to read and write them, and a separate
 * struct ltchars for the "literal next", "suspend" and "reprint" characters.
 * It has been deprecated everywhere for a very long time and does not exist
 * here at all.
 *
 * What a program wants from this header is usually not the ioctl calls but the
 * struct, because it declares a variable of that type at file scope. Older code
 * frequently carries its own fallback definition guarded on whether <sgtty.h>
 * was found - which is the right instinct, and the reason this header can be so
 * small: declare the type, and define the guard under the same name the real
 * header uses - _SGTTY_H_ - because that is what a program's fallback tests.
 * A guard with tidier naming would leave the program defining the struct a
 * second time and failing on the duplicate, which is a confusing way to find
 * out that a shim's job includes its own spelling.
 *
 * The requests are not answered. TIOCGLTC and friends reach ioctl(), which
 * returns -1/ENOTTY for anything it cannot really do, and a program saving
 * terminal state continues without it. That is the same outcome as running on a
 * descriptor that is not a pty, and code that saves terminal state is written to
 * tolerate it.
 */

struct sgttyb
{
    char sg_ispeed;   /* input speed     */
    char sg_ospeed;   /* output speed    */
    char sg_erase;    /* erase character */
    char sg_kill;     /* kill character  */
    short sg_flags;   /* mode flags      */
};

struct ltchars
{
    char t_suspc;   /* stop process signal */
    char t_dsuspc;  /* delayed stop process signal */
    char t_rprntc;  /* reprint line */
    char t_flushc;  /* flush output (toggles) */
    char t_werasc;  /* word erase */
    char t_lnextc;  /* literal next character */
};

#endif /* _SGTTY_H_ */
