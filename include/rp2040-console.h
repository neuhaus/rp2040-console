#ifndef RP2040_CONSOLE_H
#define RP2040_CONSOLE_H

/*
 * Prepended to every translation unit of a program built with this engine,
 * by way of -include.
 *
 * The point is that a game does not have to know it is on an RP2040. What a
 * port normally does to a program's own headers - rename a function that
 * clashes with the C library, wrap an I/O call that the platform wires up
 * differently - happens here instead, once, at preprocessing time. The
 * program's sources stay what upstream wrote, which is what keeps them
 * updateable.
 *
 * Two things follow from being a forced include, and both matter:
 *
 *   It runs before anything else, including the program's own includes. So it
 *   pulls in the C library headers the program will include anyway, first,
 *   while their names are still unshadowed. A rename defined after that point
 *   cannot reach into a declaration in <stdlib.h> and quietly rewrite it.
 *
 *   It applies only to program C/C++ sources, including generated sources.
 *   Engine, FatFs and SDK sources keep their native declarations and names.
 */

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/times.h>
#include <utime.h>
#include <pico/stdlib.h>
#include <rp2040-console-system.h>
#include <rp2040-time32.h>
#ifdef RP2040_CONSOLE_SOFT_REBOOT
#include <rp2040-soft-reboot.h>
#define exit rp2040_soft_exit
#define _exit rp2040_soft_immediate_exit
#define _Exit rp2040_soft_immediate_exit
#define atexit rp2040_soft_atexit
#endif

/* Opt-in legacy time ABI, after native system/SDK declarations. These macros
 * affect application declarations and calls, never libc struct layouts.
 * Programs comparing stat timestamps still see native struct stat fields. */
#ifdef RP2040_CONSOLE_LEGACY_TIME32
#define time_t rp2040_time32_t
#define time rp2040_time32
#define localtime rp2040_localtime32
#define gmtime rp2040_gmtime32
#define localtime_r rp2040_localtime32_r
#define gmtime_r rp2040_gmtime32_r
#define ctime rp2040_ctime32
#define ctime_r rp2040_ctime32_r
#define mktime rp2040_mktime32
#define difftime rp2040_difftime32
#endif

/*
 * Input: getchar() with the tty's input processing applied.
 *
 * The SDK claims the getchar symbol itself - it links --wrap=getchar onto its
 * stdio drivers - so this cannot be done by supplying a better getchar() and
 * has to be done before compilation. A function-like macro expands only in
 * front of a '(', so a program taking the address of getchar is untouched.
 *
 * console_getchar() adds what a line discipline used to add and the SDK's
 * wrapper does not: carriage return becomes newline, and the character is
 * echoed if the program has not turned ECHO off. Without the first, Enter
 * arrives as CR and no loop waiting for '\n' ever finishes.
 */
extern int console_getchar(void);
#define getchar(...) console_getchar()

/*
 * The entry point, moved aside so that the engine can have main().
 *
 * The engine's main() is what the C runtime calls; it brings up the console,
 * the environment, the clock and the volume, and then calls this. The
 * replacement is not just tidiness: there is no command line on this device,
 * and the registers the runtime leaves in place of argc and argv are whatever
 * initialisation happened to put there, so a program that reads argv[0] is
 * following a wild pointer. The engine passes one argument, the program's own
 * name, which is all a program can sensibly want here.
 *
 * --wrap=main is the usual way to do this and does not work; see the note in
 * cmake/rp2040-console.cmake.
 *
 * One caveat, since this is a very common word: the token main is rewritten
 * wherever the preprocessor sees it. Programs almost always use it only as the
 * name of the entry point, and if one does not, the result is a compile error
 * naming the line rather than something silently wrong. A game that genuinely
 * needs main as an identifier should say so here and be handled by name.
 */
#define main rp2040_console_game_main

/*
 * Names that clash, and what to call the program's instead.
 *
 * Generated from the program's manifest, because which names clash is a fact
 * about the program rather than about this platform: a one-argument itoa() and a
 * panic() of one's own are choices one particular program made. Everything here
 * runs after the C library headers above have been read, which is what lets a
 * rename apply to the program's declarations and calls without also rewriting a
 * libc declaration it happens to share a name with.
 *
 * The file is generated even when the program declares no renames, so that this
 * include is not conditional on a build-time detail.
 */
#include <rp2040-console-renames.h>

/*
 * The name programs test for POSIX, which this C library does not define.
 *
 * newlib exports __POSIX_VISIBLE and _POSIX_C_SOURCE but not _POSIX_VERSION,
 * and _POSIX_VERSION is the name that pre-dates them in portable code and the
 * one programs still check. What they are checking, in almost every case, is
 * "should I use <termios.h>, or the older BSD and System V terminal
 * interfaces".
 *
 * On this platform that choice matters: the pre-POSIX paths want sgtty and
 * termio and drive the terminal through gtty() and stty(), none of which can
 * be honoured, while the POSIX path wants termios, which is provided and which
 * the engine acts on. Letting a program fall into the older branch because the
 * macro is missing would leave it with a worse terminal than the one it is
 * being given.
 *
 * The claim is deliberately narrow: it is about the terminal interface, which
 * is the only thing a program can be using this macro for at the point where
 * it tests it. It is not a claim that there are processes, signals, or anything
 * else POSIX has that this device does not.
 */
#ifndef _POSIX_VERSION
#define _POSIX_VERSION 200809L
#endif

#endif /* RP2040_CONSOLE_H */
