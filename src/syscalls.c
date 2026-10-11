/*
 * The non-file part of the libc surface for a terminal game on RP2040.
 *
 * newlib's stdio and friends are built on POSIX syscalls that do not exist
 * without a kernel underneath. The Pico SDK supplies _exit, _write and a
 * few others; this file supplies the rest, either faithfully (sleep(),
 * times()) or as an honest refusal for something an RP2040 cannot do, which
 * is a great deal better than pretending.
 *
 * File operations are not here: they are in fileio.c, on top of FatFs.
 * What is left is the surface a program touches without meaning to - the
 * process identity calls newlib's exit path makes, the permission calls a
 * setuid binary would use, and the clock.
 */

#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "clock.h"

#include <pico/stdlib.h>

/* --- time ------------------------------------------------------------ */

/*
 * newlib's time() wants Unix epoch seconds. Nothing on this hardware keeps
 * time, so the engine's clock answers: the date and time settled on at
 * startup, plus the elapsed time since. See clock.c.
 */
time_t time(time_t *t)
{
    time_t now = console_clock_now();

    if (t != NULL)
        *t = now;

    return now;
}

int usleep(useconds_t usec)
{
    sleep_ms((uint32_t)(usec / 1000u) + (usec % 1000u ? 1u : 0u));
    return 0;
}

/*
 * newlib provides sleep() only when a real clock/tick source is wired up.
 * On the Pico the timer is driven by sleep_ms(), so supply it: retry loops
 * and lock-waits pace themselves with this.
 */
unsigned int sleep(unsigned int seconds)
{
    sleep_ms(seconds * 1000u);
    return 0;
}

/* --- filesystem ------------------------------------------------------ */

/*
 * The real implementations are in fileio.c, which implements the POSIX
 * descriptor API (open/close/read/write/lseek/unlink/stat) on top of
 * FatFs and the SD card. Nothing filesystem-related belongs here.
 */

/* newlib declares this as mode_t umask(mode_t), not int. */
mode_t umask(mode_t mask)
{
    /* Harmless to pretend: no permissions model here. */
    return mask;
}

/* --- identity -------------------------------------------------------- */

/*
 * Single user, single process. The uid and gid are reported as 0, which makes
 * the setuid(getuid()) a program may do to drop privileges before running a
 * shell a no-op rather than an error, and makes the ownership check in a save
 * file always agree with itself.
 *
 * The types are the ones <unistd.h> declares - uid_t and gid_t, unsigned - so
 * that this definition and that declaration cannot disagree. Getting this wrong
 * is not a warning someone notices: the program sees whatever the header
 * promised and reads the return value as the wrong type.
 */
uid_t getuid(void)  { return 0; }
gid_t getgid(void)  { return 0; }
uid_t geteuid(void) { return 0; }
gid_t getegid(void) { return 0; }

int setuid(uid_t uid) { (void)uid; return 0; }
int setgid(gid_t gid) { (void)gid; return 0; }

/*
 * No login concept here, so report none rather than inventing one.
 *
 * This matters more than it looks. A program that fills in a player name from
 * the environment - USER, then LOGNAME, then getlogin() - and gets a non-empty
 * answer never asks the player who they are, so every game ends up played under
 * whatever string was invented here. NULL is the honest answer and leaves the
 * choice where it belongs, which is why USER is deliberately absent from a
 * game's environment too.
 */
char *getlogin(void)
{
    return NULL;
}

/*
 * One process, so the only pid that exists is our own; anything else is from
 * a previous boot. Lock files record the pid that took them, so "no such
 * process" is how a stale lock is recognised.
 */
int kill(pid_t pid, int sig)
{
    if (pid != getpid()) {
        errno = ESRCH;
        return -1;
    }
    if (sig != 0) {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int _kill(pid_t pid, int sig) { return kill(pid, sig); }