/*
 * The few POSIX calls NetHack reaches under this configuration that the
 * console's libc does not provide.  getpid() and getentropy() already come
 * from the engine, and isatty() comes from newlib via the SDK's _isatty hook,
 * so neither is repeated here.
 */
#include <errno.h>
#include <stddef.h>
#include <unistd.h>

/*
 * NetHack only calls this to print the path of the loadable symbols file.
 * The playground is the FAT volume mounted at the root.
 */
char *
getcwd(char *buf, size_t size)
{
    if (buf == NULL || size < 2) {
        errno = ERANGE;
        return NULL;
    }
    buf[0] = '/';
    buf[1] = '\0';
    return buf;
}

/*
 * unixtty.c uses fpathconf(0, _PC_VDISABLE) for the "no character" value of
 * the termios control characters.  There is nothing to query on this tty, and
 * 0 is the conventional _POSIX_VDISABLE.
 */
long
fpathconf(int fd, int name)
{
    (void) fd;
    (void) name;
    return 0L;
}
