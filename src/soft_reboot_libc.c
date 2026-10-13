/* newlib 4.x session context. Standard program streams are separate FILEs:
 * closing them cannot close the engine's console or destroy its FILE state. */
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdio_ext.h>
#include <locale.h>
#include <time.h>
#include <reent.h>
#include <sys/reent.h>
#include "soft_reboot_internal.h"

#if defined(_REENT_THREAD_LOCAL) || defined(__DYNAMIC_REENT__)
#error "SOFT_REBOOT requires single-threaded newlib with _impure_ptr"
#endif

struct _reent rp2040_program_reent;
static struct _reent *platform_reent;
static struct _glue *original_tail;
static FILE *preserved[16];
static size_t preserved_count;
static int context_started;

/* These caches are process-global, not reent-owned. Keep their allocations
 * outside the session sweep even when localtime()/setlocale() call them. */
extern void __real__tzset_r(struct _reent *);
extern void __real__tzset_unlocked_r(struct _reent *);
extern char *__real__setlocale_r(struct _reent *, int, const char *);

void __wrap__tzset_r(struct _reent *context)
{
    (void)context;
    __real__tzset_r(_GLOBAL_REENT);
}

void __wrap__tzset_unlocked_r(struct _reent *context)
{
    (void)context;
    __real__tzset_unlocked_r(_GLOBAL_REENT);
}

char *__wrap__setlocale_r(struct _reent *context, int category, const char *locale)
{
    char *result = __real__setlocale_r(_GLOBAL_REENT, category, locale);
    if (result == NULL) _REENT_ERRNO(context) = _REENT_ERRNO(_GLOBAL_REENT);
    return result;
}

static int is_preserved(FILE *file)
{
    for (size_t i = 0; i < preserved_count; ++i)
        if (preserved[i] == file) return 1;
    return 0;
}

int rp2040_soft_libc_begin(void)
{
    platform_reent = _impure_ptr;
    context_started = 0;
    preserved_count = 0;
    for (struct _glue *glue = &__sglue; glue; glue = glue->_next) {
        original_tail = glue;
        for (int i = 0; i < glue->_niobs; ++i) {
            if (glue->_iobs[i]._flags) {
                if (preserved_count == 16) return -1;
                preserved[preserved_count++] = &glue->_iobs[i];
            }
        }
    }
    _REENT_INIT_PTR(&rp2040_program_reent);
    _impure_ptr = &rp2040_program_reent;
    context_started = 1;
    /* fdopen() creates entries in __sglue; allocations are session-owned. */
    FILE *input = fdopen(0, "r");
    FILE *output = fdopen(1, "w");
    FILE *error = fdopen(2, "w");
    if (input) _REENT_STDIN(&rp2040_program_reent) = input;
    if (output) _REENT_STDOUT(&rp2040_program_reent) = output;
    if (error) _REENT_STDERR(&rp2040_program_reent) = error;
    if (!input || !output || !error) return -1;
    setvbuf(input, NULL, _IONBF, 0);
    setvbuf(output, NULL, _IONBF, 0);
    setvbuf(error, NULL, _IONBF, 0);
    srand(1);
    srandom(1);
    setlocale(LC_ALL, "C");
    return 0;
}

int rp2040_soft_libc_end(int flush)
{
    if (!context_started) return 0;
    int failed = 0;
    /* Do not use libc exit(): its global handler closes platform stdio too. */
    for (struct _glue *glue = &__sglue; glue; glue = glue->_next)
        for (int i = 0; i < glue->_niobs; ++i) {
            FILE *file = &glue->_iobs[i];
            if (file->_flags && !is_preserved(file)) {
                if (!flush) __fpurge(file);
                if (fclose(file) != 0) failed = 1;
            }
        }
    /* Detach session-owned FILE pool nodes before the allocation sweep. */
    original_tail->_next = NULL;
    _impure_ptr = platform_reent;
    /* _reclaim_reent does nothing if ptr == _impure_ptr. Switch back first.
     * It deliberately omits _sig_func; no OS signal can arrive here, so free it. */
    free(_REENT_SIG_FUNC(&rp2040_program_reent));
    _REENT_SIG_FUNC(&rp2040_program_reent) = NULL;
    _REENT_CLEANUP(&rp2040_program_reent) = NULL; /* FILEs were closed above. */
    _reclaim_reent(&rp2040_program_reent);
    return failed ? -1 : 0;
}
