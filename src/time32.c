#include <errno.h>
#include <limits.h>
#include <stddef.h>
#include "rp2040-time32.h"

static rp2040_time32_t narrow(time_t value)
{
    if (value < INT32_MIN || value > INT32_MAX) {
        errno = EOVERFLOW;
        return (rp2040_time32_t)-1;
    }
    return (rp2040_time32_t)value;
}

rp2040_time32_t rp2040_time32(rp2040_time32_t *out)
{
    time_t now = time(NULL);
    rp2040_time32_t result = narrow(now);
    /* Do not overwrite the caller's value on range failure. */
    if (out != NULL && now >= INT32_MIN && now <= INT32_MAX)
        *out = result;
    return result;
}

#define CONVERT(name, native, result) \
result name(const rp2040_time32_t *value) \
{ \
    if (value == NULL) { errno = EFAULT; return NULL; } \
    time_t wide = *value; \
    return native(&wide); \
}

CONVERT(rp2040_localtime32, localtime, struct tm *)
CONVERT(rp2040_gmtime32, gmtime, struct tm *)
CONVERT(rp2040_ctime32, ctime, char *)

#define CONVERT_R(name, native, result, output) \
result name(const rp2040_time32_t *value, output out) \
{ \
    if (value == NULL || out == NULL) { errno = EFAULT; return NULL; } \
    time_t wide = *value; \
    return native(&wide, out); \
}

CONVERT_R(rp2040_localtime32_r, localtime_r, struct tm *, struct tm *)
CONVERT_R(rp2040_gmtime32_r, gmtime_r, struct tm *, struct tm *)
CONVERT_R(rp2040_ctime32_r, ctime_r, char *, char *)

rp2040_time32_t rp2040_mktime32(struct tm *value)
{
    if (value == NULL) { errno = EFAULT; return -1; }
    return narrow(mktime(value));
}

double rp2040_difftime32(rp2040_time32_t end, rp2040_time32_t start)
{
    /* Convert before subtracting: the difference can exceed INT32_MAX. */
    return (double)end - (double)start;
}
