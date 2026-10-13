/* Linux syscall bridge for executing ARM/newlib tests under qemu-arm.
 * It is not part of device firmware. No SDK, USB or MMIO is emulated here. */
#include <errno.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

static long call3(long number, long a, long b, long c)
{
    register long r0 __asm__("r0") = a;
    register long r1 __asm__("r1") = b;
    register long r2 __asm__("r2") = c;
    register long r7 __asm__("r7") = number;
    __asm__ volatile("svc 0" : "+r"(r0) : "r"(r1), "r"(r2), "r"(r7) : "memory");
    if (r0 < 0 && r0 >= -4095) { errno = -r0; return -1; }
    return r0;
}
extern void *_sbrk(int);
size_t test_heap_break(void)
{
    extern char end;
    return (uintptr_t)_sbrk(0) - (uintptr_t)&end;
}
int _write(int fd, const void *data, size_t size) { return call3(4, fd, (long)data, size); }
int _read(int fd, void *data, size_t size) { return call3(3, fd, (long)data, size); }
int _close(int fd) { return fd < 3 ? 0 : call3(6, fd, 0, 0); }
int _open(const char *path, int flags, ...)
{
    int linux_flags = flags & O_ACCMODE;
    if (flags & O_CREAT) linux_flags |= 64;
    if (flags & O_EXCL) linux_flags |= 128;
    if (flags & O_TRUNC) linux_flags |= 512;
    if (flags & O_APPEND) linux_flags |= 1024;
    return call3(5, (long)path, linux_flags, 0600);
}
int _unlink(const char *path) { return call3(10, (long)path, 0, 0); }
off_t _lseek(int fd, off_t position, int whence) { return call3(19, fd, position, whence); }
int _fstat(int fd, struct stat *out)
{
    *out = (struct stat){0};
    out->st_mode = fd < 3 ? S_IFCHR | 0666 : S_IFREG | 0600;
    out->st_blksize = 512;
    return 0;
}
int _isatty(int fd) { return fd < 3; }
int _fcntl(int fd, int command, ...) { (void)command; return fd == 0 ? O_RDONLY : O_WRONLY; }
int _getpid(void) { return 1; }
int _kill(int pid, int signal) { (void)pid; (void)signal; errno = ENOSYS; return -1; }
void _exit(int status) { call3(1, status, 0, 0); for (;;) {} }
