#ifndef RP2040_CONSOLE_TERMCAP_H
#define RP2040_CONSOLE_TERMCAP_H

/*
 * termcap.
 *
 * There is no termcap library here and no /etc/termcap to read, so a program
 * that drives a terminal the classic way has nothing to include and nothing
 * to link. This header supplies the declarations; the engine supplies the
 * implementation, answering the queries from a built-in description of the
 * terminal this console actually behaves like.
 *
 * The ABI is the classic one, including the pieces modern implementations
 * dropped: PC is the padding character termcap fills into a delay, UP and BC
 * are the cursor-backward sequences for the two ways of getting there, and
 * ospeed is not a baud rate but the small BSD index used to look up padding
 * tables. Getting those types wrong is not a compile error but a silent
 * memory error, so they are spelled out rather than tidied.
 */

extern char PC;
extern char *UP;
extern char *BC;
extern short ospeed;

int tgetent(char *bp, const char *name);
int tgetflag(const char *id);
int tgetnum(const char *id);
char *tgetstr(const char *id, char **area);

int tputs(const char *str, int affcnt, int (*putc_fn)(int));
char *tgoto(const char *cap, int col, int row);

#endif /* RP2040_CONSOLE_TERMCAP_H */
