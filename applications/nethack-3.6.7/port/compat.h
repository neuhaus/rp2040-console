/*
 * Force-included ahead of every NetHack translation unit.
 *
 * newlib already declares the libc calls NetHack uses, but NetHack's own
 * <system.h> re-declares several of them with pre-ANSI prototypes (lrand48,
 * srandom, exit, ...); a modern gcc rejects the mismatch.  Pre-including the
 * real headers and defining SYSTEM_H suppresses NetHack's copy.
 *
 * system.h would also have provided the E (extern) and SIG_RET_TYPE macros
 * that the rest of the sources rely on, so supply those here.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <time.h>
#include <signal.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define SYSTEM_H

#define E extern
#define SIG_RET_TYPE void (*)(int)

/* The data librarian can present a compressed entry as a stdio FILE*; the
 * symbol-set reader wants stdio rather than the dlb_* calls.  Implemented in
 * port/nhdlb.c. */
FILE *nhdlb_memopen(const char *name);

/* NetHack's files.c sizes a path buffer with PATH_MAX.  Suppressing
 * system.h drops whatever header used to bring it in; newlib's <limits.h>
 * does not define it either. */
#ifndef PATH_MAX
#define PATH_MAX 1024
#endif

/*
 * NetHack's tty setup asks fpathconf(0, _PC_VDISABLE) for the "disable"
 * control character.  POSIX defines the request; some newlib builds do not.
 */
#ifndef _PC_VDISABLE
#define _PC_VDISABLE 0
#endif

/*
 * Feature toggles that must agree between the program and the host tools
 * that generate headers it compiles against.
 *
 * onames.h maps object names to array indices, and it is written by a host
 * makedefs built from objects.c.  objects.c drops its "mail" scroll under
 * NOMAIL, so a generator built with mail enabled numbers every later object
 * one too high; the target then has a LAST_GEM pointing past jade, and
 * o_init.c's setgemprobs() clobbers the wrong gem (the game aborts with
 * "init-prob error").  Defining the toggles here - in the header this recipe
 * force-includes into both the program and its host generators - is what
 * keeps the two views of objects.c identical.
 */
#define NOMAIL
#define NOSHELL
