#ifndef RP2040_CONSOLE_CURSES_H
#define RP2040_CONSOLE_CURSES_H

/*
 * THIS IS NOT CURSES.
 *
 * It exists because terminal programs of the termcap era include <curses.h>
 * for one or two function prototypes while doing all their own screen work,
 * and this toolchain has no curses at all - not the header, not the library.
 * Without a header by that name the program does not compile, and asking
 * every program to notice that is a poor use of a port.
 *
 * Nothing in here provides screen management. Declare only what a caller
 * actually references and can genuinely be given, and keep the list short
 * deliberately: the moment this file grows a window or pad type, it has
 * stopped being a compatibility shim and started being a promise nobody is
 * keeping.
 *
 * delay_output() is declared because it is a curses entry point that some
 * programs call for a padded delay and others define themselves behind a
 * feature test. Where the program supplies its own, that definition is what
 * gets used and this header is only the prototype.
 */

int delay_output(int ms);

#endif /* RP2040_CONSOLE_CURSES_H */
