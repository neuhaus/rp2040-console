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
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <pico/stdlib.h>

#include "ff.h"
#include "clock.h"
#include "console.h"
#include "mtime.h"
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

bool storage_is_available(void)
{
    return storage_available;
}

static struct {
    bool     used;
    FIL      fil;
    int      flags;
    /* Directory time as of open(), for fstat(): a descriptor and a path to
     * the same file must agree, which is what save freshness is compared on.
     */
    time_t   mtime;
    /*
     * Path as opened, so close() can record the session date for it (see
     * mtime.h). Skipped when it does not fit, which fails safe to the
     * directory time.
     */
    char     path[96];
    bool     have_path;
} open_files[MAX_OPEN_FILES];

static bool preserved_files[MAX_OPEN_FILES];

/*
 * Time conversion is approximate: a packed FAT stamp is a local time with no
 * timezone, so fold it to a monotonic-ish value rather than a real date. The
 * game only compares two of these against each other, so every path must fold
 * the same way - one shared function for directory stamps and session dates.
 */
static time_t fold_datetime(int year, int mon, int day,
                            int hour, int min, int sec)
{
    /*
     * 64-bit arithmetic throughout: the folded value is far past 2^31, and
     * time_t is 64 bits, so 32-bit math would wrap first and misorder.
     */
    uint64_t t = (uint64_t)year * 31536000u +
                 (uint64_t)mon * 2592000u +
                 (uint64_t)day * 86400u +
                 (uint64_t)hour * 3600u +
                 (uint64_t)min * 60u + (uint64_t)sec;

    return (time_t)t;
}

static time_t filinfo_mtime(const FILINFO *fi)
{
    uint32_t date = fi->fdate;
    uint32_t time = fi->ftime;

    return fold_datetime((int)((date >> 9) & 0x7FU) + 1980,
                         (int)((date >> 5) & 0x0FU),
                         (int)(date & 0x1FU),
                         (int)((time >> 11) & 0x1FU),
                         (int)((time >> 5) & 0x3FU),
                         (int)(time & 0x1FU) * 2);
}

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
    st->st_mtime  = filinfo_mtime(fi);
}

/*
 * Files start at 3, because 0, 1 and 2 are the console. Handing one of those to
 * open() makes a file share a destination with the screen or the keyboard, which
 * is how a save file ends up printed instead of written.
 */
#define FIRST_FILE_FD 3

static int alloc_fd(void)
{
    for (int i = FIRST_FILE_FD; i < MAX_OPEN_FILES; i++) {
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
    open_files[fd].have_path = (strlen(path) < sizeof(open_files[fd].path));
    if (open_files[fd].have_path)
        strcpy(open_files[fd].path, path);

    /*
     * Cache the time for fstat(): the remembered session date first, then
     * the directory, which is the truth for files placed from the host.
     */
    {
        FILINFO fi;
        int found;

        open_files[fd].mtime = mtime_lookup(path, &found);
        if (!found)
            open_files[fd].mtime = (f_stat(path, &fi) == FR_OK)
                                 ? filinfo_mtime(&fi) : (time_t)0;
    }
    return fd;
}

int creat(const char *path, mode_t mode)
{
    (void)mode;
    return open(path, O_RDWR | O_CREAT | O_TRUNC, 0666);
}

/*
 * The session clock in the same folded form filinfo_mtime() produces, so the
 * two compare. The real epoch is handed to mtime_store() alongside it, so a
 * listing can show a date rather than the comparison encoding.
 */
static time_t fold_session(time_t now)
{
    struct tm *tm = gmtime(&now);
    int year;

    if (tm == NULL)
        return (time_t)0;

    year = tm->tm_year + 1900;
    if (year < 1980)
        year = 1980;
    if (year > 2107)
        year = 2107;

    return fold_datetime(year, tm->tm_mon + 1, tm->tm_mday,
                         tm->tm_hour, tm->tm_min, tm->tm_sec);
}

int close(int fd)
{
    /* Console descriptors are virtual: never shut down USB on fclose(). */
    if (fd >= 0 && fd < FIRST_FILE_FD) return 0;
    if (fd < 0 || fd >= MAX_OPEN_FILES || !open_files[fd].used) {
        errno = EBADF;
        return -1;
    }

    FRESULT result = f_close(&open_files[fd].fil);
    if (result != FR_OK) { errno = EIO; return -1; }

    /*
     * Remember when this was written; the directory entry cannot (it has no
     * RTC to ask). Read-only handles leave no trace.
     */
    if ((open_files[fd].flags & O_ACCMODE) != O_RDONLY &&
        open_files[fd].have_path) {
        time_t now = console_clock_now();

        mtime_store(open_files[fd].path, fold_session(now), now);
    }

    open_files[fd].used = false;
    return 0;
}

int _close(int fd) { return close(fd); }

void console_files_begin_session(void)
{
    for (int fd = FIRST_FILE_FD; fd < MAX_OPEN_FILES; ++fd)
        preserved_files[fd] = open_files[fd].used;
}

int console_files_close_session(void)
{
    int result = 0;
    for (int fd = FIRST_FILE_FD; fd < MAX_OPEN_FILES; ++fd)
        if (open_files[fd].used && !preserved_files[fd] && close(fd) != 0) result = -1;
    return result;
}

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

/*
 * POSIX advisory locks, the fcntl() counterpart of flock() above.  NetHack's
 * UNIX code takes an F_WRLCK on its "perm" file for the duration of a game;
 * with one program there is nobody to conflict with, so granting it is the
 * honest answer rather than a stub.  F_GETLK reports the region unlocked.
 *
 * Getting this right matters more than it looks.  If fcntl() fails, NetHack
 * falls back to creating <file>_lock files; those survive a crash or a power
 * cut on a FAT volume and would then refuse to start the next game until the
 * volume was rewritten from a host.  An advisory lock, granted here, leaves
 * nothing behind.
 */
int fcntl(int fd, int cmd, ...)
{
    va_list ap;

    if (fd < 0 || fd >= MAX_OPEN_FILES || !open_files[fd].used) {
        errno = EBADF;
        return -1;
    }

    va_start(ap, cmd);
    switch (cmd) {
    case F_GETLK: {
        struct flock *fl = va_arg(ap, struct flock *);

        /* Nothing else can hold a lock, so no lock can conflict. */
        if (fl != NULL)
            fl->l_type = F_UNLCK;
        va_end(ap);
        return 0;
    }
    case F_SETLK:
    case F_SETLKW:
        (void)va_arg(ap, struct flock *);
        va_end(ap);
        return 0;
    default:
        va_end(ap);
        errno = EINVAL;
        return -1;
    }
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

    /*
     * f_lseek reports an FRESULT (0..FR_INVALID_PARAMETER), never a negative
     * value, so the failure test has to be against FR_OK - "< 0" is a
     * constant false and would swallow every seek error.
     */
    if (f_lseek(fil, (FSIZE_t)base + (FSIZE_t)offset) != FR_OK) {
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
    FRESULT fr;

    if (!storage_available) {             /* nothing mounted, so nothing to remove */
        errno = ENOENT;
        return -1;
    }

    fr = f_stat(path, &fi);

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
    mtime_drop(path);
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

int rename(const char *oldpath, const char *newpath)
{
    FRESULT fr;

    if (oldpath == NULL || newpath == NULL) {
        errno = EFAULT;
        return -1;
    }
    if (!storage_available) {
        errno = ENOENT;
        return -1;
    }

    fr = f_rename(oldpath, newpath);
    if (fr != FR_OK) {
        TRACE("fileio: rename(\"%s\", \"%s\") -> %s (%d)\n",
              oldpath, newpath, fr_name(fr), fr);
        errno = (fr == FR_NO_FILE || fr == FR_NO_PATH) ? ENOENT :
                (fr == FR_EXIST)                       ? EEXIST :
                                                         EIO;
        return -1;
    }
    mtime_move(oldpath, newpath);
    return 0;
}

int _rename(const char *oldpath, const char *newpath)
{
    return rename(oldpath, newpath);
}

int fsync(int fd)
{
    if (fd < FIRST_FILE_FD || fd >= MAX_OPEN_FILES ||
        !open_files[fd].used) {
        errno = EBADF;
        return -1;
    }

    if (f_sync(&open_files[fd].fil) != FR_OK) {
        errno = EIO;
        return -1;
    }

    /* Same correction as in close(): f_sync re-stamps as well. */
    if ((open_files[fd].flags & O_ACCMODE) != O_RDONLY &&
        open_files[fd].have_path) {
        time_t now = console_clock_now();

        mtime_store(open_files[fd].path, fold_session(now), now);
    }

    return 0;
}

int _fsync(int fd) { return fsync(fd); }

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

    /*
     * The remembered session date wins for files made here; see mtime.h.
     * Host-placed files keep the directory time.
     */
    {
        int found;
        time_t remembered = mtime_lookup(path, &found);

        if (found)
            st->st_mtime = remembered;
    }
    TRACE("fileio: stat(\"%s\") -> mtime %lld\n", path,
          (long long)st->st_mtime);
    return 0;
}

int _stat(const char *path, struct stat *st) { return stat(path, st); }

int fstat(int fd, struct stat *st)
{
    /* The console is a character device; this is how isatty() is answered. */
    if (fd >= 0 && fd < FIRST_FILE_FD) {
        memset(st, 0, sizeof(*st));
        st->st_mode = S_IFCHR | 0666;
        return 0;
    }

    if (fd < 0 || fd >= MAX_OPEN_FILES || !open_files[fd].used) {
        errno = EBADF;
        return -1;
    }

    /*
     * f_stat() takes a path, not an open handle, so the descriptor is filled
     * from the handle itself. The size is live - it grows as the file is
     * written, which is what a caller stat'ing its own open save file is
     * really asking - and the time is what open() resolved: the remembered
     * session date, or the directory stamp when there is none.
     */
    memset(st, 0, sizeof(*st));
    st->st_mode    = S_IFREG | 0666;
    st->st_size    = (size_t)f_size(&open_files[fd].fil);
    st->st_blksize = 512;
    st->st_blocks  = (st->st_size + 511) / 512;
    st->st_mtime   = open_files[fd].mtime;
    TRACE("fileio: fstat(%d) -> size %lu mtime %lld\n",
          fd, (unsigned long)st->st_size, (long long)st->st_mtime);
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

/*
 * Change the working directory, via FatFs (built with FF_FS_RPATH).
 * Failure carries an errno, so a program told it cannot reach its data can
 * say which directory that was instead of failing obscurely.
 */
int chdir(const char *path)
{
    FRESULT fr;

    if (path == NULL) {
        errno = EFAULT;
        return -1;
    }
    if (!storage_available) {             /* nothing to be inside */
        errno = ENOENT;
        return -1;
    }

    fr = f_chdir(path);
    if (fr != FR_OK) {
        TRACE("fileio: chdir(\"%s\") -> %s (%d)\n", path, fr_name(fr), fr);
        errno = (fr == FR_NO_PATH || fr == FR_NO_FILE) ? ENOENT :
                (fr == FR_INVALID_NAME)                ? EINVAL :
                                                         EIO;
        return -1;
    }

    return 0;
}

int _chdir(const char *path) { return chdir(path); }
