/*
 * POSIX file-descriptor layer over FatFs.
 *
 * newlib's stdio (fopen/fread/fwrite/...) is built on open/close/read/
 * write/lseek, none of which exist without a kernel. This provides them
 * on top of FatFs so that both the game's direct calls and newlib's
 * internal ones work.
 *
 * Descriptors are a small fixed pool of open FIL handles - the game
 * never has more than a couple of files open at once, and a fixed pool
 * avoids a malloc dependency in a system with no heap.
 *
 * FatFs gives us open-file handles, not descriptors, so this maps
 * fd <-> FIL and keeps FatFs's own seek position.
 */

#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <pico/stdlib.h>

#include "ff.h"
#include "console.h"
#include "storage.h"
#include "trace.h"

/*
 * The game opens at most a handful of files at once (save file, data
 * file, lock file, one help file). 8 is comfortable.
 */
#define MAX_OPEN_FILES 8

/*
 * Whether a filesystem is actually mounted.
 *
 * With no SD card (or a card that failed to mount) every file operation
 * fails. Returning EIO for those is technically defensible but is fatal
 * in practice: the game tests for ENOENT to mean "no such file, carry
 * on", and treats anything else as an error it must abort on. Three
 * separate startup exits came from this - the game.lock probe,
 * gethdate(), and the per-level lock loop in getlock().
 *
 * So when there is no volume, report ENOENT for everything. For an
 * absent filesystem that is the honest answer: nothing exists.
 */
static bool storage_available = false;

void storage_set_available(bool available)
{
    storage_available = available;
}

static struct {
    bool     used;
    FIL      fil;
    int      flags;
} open_files[MAX_OPEN_FILES];

/* Fill in a struct stat from FatFs file information. */
static void stat_from_filinfo(struct stat *st, const FILINFO *fi)
{
    memset(st, 0, sizeof(*st));

    if (fi->fattrib & AM_DIR)
        st->st_mode = S_IFDIR | 0777;
    else
        st->st_mode = S_IFREG | 0666;

    st->st_size   = fi->fsize;
    st->st_blksize = 512;
    st->st_blocks = (fi->fsize + 511) / 512;

    /*
     * Time conversion is approximate. f_getftime gives a packed FAT
     * timestamp, which is a local time with no timezone. Converting it
     * to time_t needs a calendar, so fold it to a monotonic-ish value
     * rather than pretending to be a real date. The game only compares
     * two of these against each other (lock staleness).
     */
    {
        uint32_t t = fi->ftime;
        uint32_t year  = ((t >> 25) & 0x7F) + 1980;
        uint32_t month = (t >> 21) & 0x0F;
        uint32_t day   = (t >> 16) & 0x1F;
        uint32_t min   = (t >> 5)  & 0x3F;
        uint32_t sec   = (t & 0x1F) * 2;

        st->st_mtime = (time_t)(year * 31536000u + month * 2592000u +
                                day * 86400u + min * 60u + sec);
    }
}

static int alloc_fd(void)
{
    for (int i = 0; i < MAX_OPEN_FILES; i++) {
        if (!open_files[i].used) {
            open_files[i].used = true;
            memset(&open_files[i].fil, 0, sizeof(open_files[i].fil));
            return i;
        }
    }
    errno = EMFILE;
    return -1;
}

static BYTE fat_flags(int posix_flags)
{
    BYTE mode = 0;

    /* FatFs uses an access bitmask (FA_READ | FA_WRITE), not the
     * mutually-exclusive FA_READ_ONLY/FA_READ_WRITE pair. */
    if ((posix_flags & O_ACCMODE) != O_WRONLY)
        mode |= FA_READ;
    if ((posix_flags & O_ACCMODE) != O_RDONLY)
        mode |= FA_WRITE;

    /*
     * The open mode is a single 4-bit field, so O_TRUNC, O_APPEND and
     * O_CREAT have to be prioritised rather than OR'd together -
     * FA_OPEN_APPEND (0x30) already includes FA_OPEN_ALWAYS (0x10).
     */
    if (posix_flags & O_APPEND)
        mode |= FA_OPEN_APPEND;
    else if (posix_flags & O_TRUNC)
        mode |= FA_CREATE_ALWAYS;
    else if ((posix_flags & O_CREAT) && (posix_flags & O_EXCL))
        mode |= FA_CREATE_NEW;
    else if (posix_flags & O_CREAT)
        mode |= FA_OPEN_ALWAYS;
    else
        mode |= FA_OPEN_EXISTING;

    return mode;
}

/*
 * Human-readable FatFs result, so a failure names itself.
 *
 * The errno mapping below ends in a catch-all default of EIO, so any
 * FRESULT that mapping does not know about arrives as a bare I/O error.
 * Reporting the FatFs name alongside it separates "the volume was never
 * mounted" from "this sector is bad", which is the difference between a
 * build problem and a hardware one.
 */
static const char *fr_name(FRESULT fr)
{
    switch (fr) {
    case FR_OK:                 return "FR_OK";
    case FR_DISK_ERR:           return "FR_DISK_ERR";
    case FR_INT_ERR:            return "FR_INT_ERR";
    case FR_NOT_READY:          return "FR_NOT_READY";
    case FR_NO_FILE:            return "FR_NO_FILE";
    case FR_NO_PATH:            return "FR_NO_PATH";
    case FR_INVALID_NAME:       return "FR_INVALID_NAME";
    case FR_DENIED:             return "FR_DENIED";
    case FR_EXIST:              return "FR_EXIST";
    case FR_INVALID_OBJECT:     return "FR_INVALID_OBJECT";
    case FR_WRITE_PROTECTED:    return "FR_WRITE_PROTECTED";
    case FR_INVALID_DRIVE:      return "FR_INVALID_DRIVE";
    case FR_NOT_ENABLED:        return "FR_NOT_ENABLED";
    case FR_NO_FILESYSTEM:      return "FR_NO_FILESYSTEM";
    case FR_MKFS_ABORTED:       return "FR_MKFS_ABORTED";
    case FR_TIMEOUT:            return "FR_TIMEOUT";
    case FR_LOCKED:             return "FR_LOCKED";
    case FR_NOT_ENOUGH_CORE:    return "FR_NOT_ENOUGH_CORE";
    case FR_TOO_MANY_OPEN_FILES: return "FR_TOO_MANY_OPEN_FILES";
    case FR_INVALID_PARAMETER:  return "FR_INVALID_PARAMETER";
    default:                    return "FR_?";
    }
}

int open(const char *path, int flags, ...)
{
    int fd;

    if (!storage_available) {
        errno = ENOENT;
        return -1;
    }

    fd = alloc_fd();
    if (fd < 0)
        return -1;

    FRESULT fr = f_open(&open_files[fd].fil, path, fat_flags(flags));
    if (fr != FR_OK) {
        open_files[fd].used = false;

        /*
         * Traced, not printed: a failed open is a normal question here. A
         * program looking for a save file it has not written yet is not
         * reporting a fault, and the console belongs to the game.
         */
        TRACE("fileio: open(\"%s\", 0x%x) -> %s (%d)\n",
               path, flags, fr_name(fr), (int)fr);

        /* Map FatFs errors onto errno the caller checks. */
        switch (fr) {
        case FR_NO_FILE:
        case FR_NO_PATH:
            errno = ENOENT;
            break;
        case FR_EXIST:
            errno = EEXIST;
            break;
        case FR_INVALID_NAME:
            errno = EINVAL;
            break;
        case FR_DENIED:
            errno = EACCES;
            break;
        default:
            errno = EIO;
            break;
        }
        return -1;
    }

    open_files[fd].flags = flags;
    return fd;
}

int creat(const char *path, mode_t mode)
{
    (void)mode;
    return open(path, O_RDWR | O_CREAT | O_TRUNC, 0666);
}

int close(int fd)
{
    if (fd < 0 || fd >= MAX_OPEN_FILES || !open_files[fd].used) {
        errno = EBADF;
        return -1;
    }

    f_close(&open_files[fd].fil);
    open_files[fd].used = false;
    return 0;
}

int _close(int fd) { return close(fd); }

/*
 * newlib reaches the filesystem through the _-prefixed syscalls, not
 * through POSIX names. _open_r() - the reentrant entry point that
 * fopen() calls - forwards to _open(), not open():
 *
 *     _open_r(reent, path, flags, mode)  ->  _open(path, flags, 0)
 *
 * It also passes mode as 0 and ignores the caller's, so mode is
 * discarded here too. The game's open() above ignores mode anyway,
 * since FAT has none.
 *
 * Defining _open is what makes fopen() work at all. Without it the call
 * falls through to the SDK's stub and fopen() returns NULL, which the
 * caller reads as "that file is not there" rather than "there is no
 * syscall here". Every FILE* path in a game - score files, help text, saves
 * - fails that way, silently, and each one looks like a data problem.
 */
int _open(const char *path, int flags, int mode)
{
    (void)mode;
    return open(path, flags);
}

int read(int fd, void *buf, size_t count)
{
    /*
     * stdin: the console.
     *
     * A terminal read, so the first byte blocks and the rest of the request
     * takes whatever is already waiting rather than holding the caller until
     * its count is satisfied. Line-buffered semantics are the closest honest
     * imitation of what a tty would hand back.
     */
    if (fd == 0)
        return console_read(buf, count);

    if (fd < 0 || fd >= MAX_OPEN_FILES || !open_files[fd].used) {
        errno = EBADF;
        return -1;
    }

    UINT br = 0;
    FRESULT fr = f_read(&open_files[fd].fil, buf, (UINT)count, &br);
    if (fr != FR_OK) {
        errno = EIO;
        return -1;
    }
    return (int)br;
}

int _read(int fd, void *buf, size_t count) { return read(fd, buf, count); }

int write(int fd, const void *buf, size_t count)
{
    /*
     * stdout and stderr are not files, and this is the one place that has to
     * know it.
     *
     * putchar(), puts() and printf() never get here - the SDK --wraps them
     * onto its stdio drivers. fputs(), fprintf() and fwrite() do, because they
     * were not wrapped, and they arrive with fd 1 or 2 expecting the write to
     * appear on the screen. If the descriptor check below is the first thing
     * they meet, the write fails as a bad descriptor inside a stdio call that
     * most programs do not check, and the text simply never shows up while
     * everything the program did with printf() does.
     */
    if (fd == 1 || fd == 2) {
        console_write(buf, count);
        return (int)count;
    }

    if (fd < 0 || fd >= MAX_OPEN_FILES || !open_files[fd].used) {
        errno = EBADF;
        return -1;
    }

    UINT bw = 0;
    FRESULT fr = f_write(&open_files[fd].fil, buf, (UINT)count, &bw);
    if (fr != FR_OK) {
        errno = EIO;
        return -1;
    }
    return (int)bw;
}

int _write(int fd, const void *buf, size_t count) { return write(fd, buf, count); }

/*
 * Advisory file locking.
 *
 * There is one program running, it cannot fork, and there is no other player,
 * so an exclusive lock is always grantable and a non-blocking request never has
 * to fail. Refusing flock() because "FAT has no locking" would be the wrong
 * answer: the point of the call, for the program using it, is to find out
 * whether somebody else has the file open, and the answer is always no.
 *
 * Locks do not survive a reboot, which is exactly right - nothing else does
 * either, and a reset is what happens in place of the crash that would
 * otherwise leave a stale lock behind.
 *
 * The descriptor is still validated, because a program that passes a closed or
 * bogus fd wants to hear about that, and getting "lock acquired" would send it
 * off down the wrong path.
 */
int flock(int fd, int operation)
{
    (void)operation;

    if (fd < 0 || fd >= MAX_OPEN_FILES || !open_files[fd].used) {
        errno = EBADF;
        return -1;
    }

    return 0;
}

long lseek(int fd, long offset, int whence)
{
    if (fd < 0 || fd >= MAX_OPEN_FILES || !open_files[fd].used) {
        errno = EBADF;
        return -1;
    }

    FIL *fil = &open_files[fd].fil;
    FSIZE_t base = 0;

    switch (whence) {
    case SEEK_SET:
        break;
    case SEEK_CUR:
        base = f_tell(fil);
        break;
    case SEEK_END:
        base = f_size(fil);
        break;
    default:
        errno = EINVAL;
        return -1;
    }

    if (f_lseek(fil, (FSIZE_t)base + (FSIZE_t)offset) < 0) {
        errno = EINVAL;
        return -1;
    }

    return (long)f_tell(fil);
}

/*
 * _lseek_r() forwards to _lseek(), same as _open_r() forwards to
 * _open(). Without it a large read through stdio stops at the first
 * buffer boundary, because newlib refills by seeking: the help text and
 * the rumor file are both well past a single buffer.
 */
long _lseek(int fd, long offset, int whence) { return lseek(fd, offset, whence); }

int unlink(const char *path)
{
    FILINFO fi;
    FRESULT fr = f_stat(path, &fi);

    if (fr != FR_OK) {
        errno = ENOENT;
        return -1;
    }

    /*
     * f_unlink refuses directories on FAT. The game only ever removes
     * plain files (bones_xx, lock files), so treat a directory as a
     * permission failure rather than silently succeeding.
     */
    if (fi.fattrib & AM_DIR) {
        errno = EISDIR;
        return -1;
    }

    fr = f_unlink(path);
    if (fr != FR_OK) {
        errno = (fr == FR_DENIED) ? EACCES : EIO;
        return -1;
    }
    return 0;
}

int _unlink(const char *path) { return unlink(path); }

/*
 * FAT has no hard links, so there is nothing honest to do here but say so.
 *
 * link() on a unix filesystem is the classic way to take a lock file, and a
 * program reaching for it here should use O_CREAT|O_EXCL instead, which is
 * the same atomic create-if-absent and is what FatFs actually guarantees.
 * Returning success would be actively harmful: the caller believes it holds
 * a lock that was never taken.
 */
int _link(const char *path, const char *newpath)
{
    (void)path;
    (void)newpath;
    errno = ENOSYS;
    return -1;
}

int link(const char *path, const char *newpath) { return _link(path, newpath); }

int stat(const char *path, struct stat *st)
{
    FILINFO fi;
    FRESULT fr;

    if (!storage_available) {
        errno = ENOENT;
        return -1;
    }

    fr = f_stat(path, &fi);

    if (fr != FR_OK) {
        errno = ENOENT;
        return -1;
    }

    stat_from_filinfo(st, &fi);
    return 0;
}

int _stat(const char *path, struct stat *st) { return stat(path, st); }

int fstat(int fd, struct stat *st)
{
    if (fd < 0 || fd >= MAX_OPEN_FILES || !open_files[fd].used) {
        errno = EBADF;
        return -1;
    }

    FILINFO fi;
    if (f_stat(open_files[fd].fil.flag == 0 ? "" : NULL, &fi) != FR_OK) {
        /*
         * f_stat takes a path, not a handle, so re-stat is not possible
         * from a descriptor alone. Derive what we can from the handle.
         */
        memset(st, 0, sizeof(*st));
        st->st_mode  = S_IFREG | 0666;
        st->st_size  = (size_t)f_size(&open_files[fd].fil);
        st->st_blksize = 512;
        return 0;
    }

    stat_from_filinfo(st, &fi);
    return 0;
}

int access(const char *path, int mode)
{
    (void)mode;

    FILINFO fi;

    if (!storage_available) {
        errno = ENOENT;
        return -1;
    }

    if (f_stat(path, &fi) != FR_OK) {
        errno = ENOENT;
        return -1;
    }
    return 0;
}

int chdir(const char *path)
{
    /*
     * No directory support, and none is needed: the working directory is the
     * root of the volume and stays there. A program that asks to change
     * directory is told it cannot, rather than being told it succeeded while
     * carrying on somewhere it did not intend to be - a program that checks has
     * a right to know, and one that does not is no worse off either way.
     */
    (void)path;
    errno = ENOSYS;
    return -1;
}

int _chdir(const char *path) { return chdir(path); }