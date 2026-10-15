/* Exercise the shared wrappers against controlled FatFs responses. Include
 * the implementation so descriptor states can be tested without USB/flash.
 * Rename host-incompatible syscall signatures after reading host headers. */
#include <unistd.h>
#define read fileio_test_read
#define write fileio_test_write
#define lseek fileio_test_lseek
#define getcwd fileio_test_getcwd
#define fpathconf fileio_test_fpathconf
#define chdir fileio_test_chdir
#include "../src/fileio.c"
#include <assert.h>

static const char *working_directory = "0:/";
static FRESULT cwd_result = FR_OK;
static unsigned cwd_calls;

FRESULT f_getcwd(char *buf, UINT size)
{
    ++cwd_calls;
    if (cwd_result != FR_OK) return cwd_result;
    if (size <= strlen(working_directory)) return FR_NOT_ENOUGH_CORE;
    strcpy(buf, working_directory);
    return FR_OK;
}

FRESULT f_chdir(const char *path)
{
    working_directory = path;
    return FR_OK;
}

int main(void)
{
    char buf[32];
    storage_set_available(false);
    errno = 0;
    assert(getcwd(buf, sizeof buf) == NULL && errno == ENOENT);
    assert(cwd_calls == 0);
    storage_set_available(true);
    assert(getcwd(NULL, sizeof buf) == NULL && errno == EFAULT);
    assert(getcwd(buf, 0) == NULL && errno == EINVAL);
    assert(getcwd(buf, 1) == NULL && errno == ERANGE);
    assert(getcwd(buf, 3) == NULL && errno == ERANGE);
    assert(cwd_calls == 0);
    assert(getcwd(buf, 4) == buf && !strcmp(buf, "0:/"));
    assert(chdir("0:/save/nested") == 0);
    assert(getcwd(buf, sizeof buf) == buf);
    assert(!strcmp(buf, "0:/save/nested"));
    assert(getcwd(buf, strlen(working_directory)) == NULL && errno == ERANGE);
    assert(getcwd(buf, strlen(working_directory) + 1) == buf);
    cwd_result = FR_DISK_ERR;
    assert(getcwd(buf, sizeof buf) == NULL && errno == EIO);
    cwd_result = FR_NO_PATH;
    assert(getcwd(buf, sizeof buf) == NULL && errno == ENOENT);

    for (int fd = 0; fd < FIRST_FILE_FD; ++fd)
        assert(fpathconf(fd, _PC_VDISABLE) == _POSIX_VDISABLE);
    assert(fpathconf(-1, _PC_VDISABLE) == -1 && errno == EBADF);
    assert(fpathconf(MAX_OPEN_FILES, _PC_VDISABLE) == -1 && errno == EBADF);
    assert(fpathconf(FIRST_FILE_FD, _PC_VDISABLE) == -1 && errno == EBADF);
    open_files[FIRST_FILE_FD].used = true;
    assert(fpathconf(FIRST_FILE_FD, _PC_VDISABLE) == -1 && errno == ENOTTY);
    assert(fpathconf(FIRST_FILE_FD, _PC_NAME_MAX) == -1 && errno == EINVAL);
    open_files[FIRST_FILE_FD].used = false;
    assert(fpathconf(FIRST_FILE_FD, _PC_VDISABLE) == -1 && errno == EBADF);
    assert(fpathconf(0, _PC_NAME_MAX) == -1 && errno == EINVAL);
    return 0;
}
