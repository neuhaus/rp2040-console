#define _POSIX_C_SOURCE 200809L
#include <assert.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "rp2040-time32.h"

static time_t fake_now;
time_t time(time_t *out) { if (out) *out = fake_now; return fake_now; }
int main(void)
{
    struct { rp2040_time32_t value; uint32_t canary; } out = { 0, 0xfeedface };
    fake_now = INT32_MAX;
    assert(rp2040_time32(&out.value) == INT32_MAX);
    assert(out.value == INT32_MAX && out.canary == 0xfeedface);
    fake_now = (time_t)INT32_MAX + 1;
    errno = 0;
    assert(rp2040_time32(&out.value) == -1 && errno == EOVERFLOW);
    assert(out.value == INT32_MAX && out.canary == 0xfeedface);
    fake_now = INT32_MIN;
    assert(rp2040_time32(NULL) == INT32_MIN);
    fake_now = (time_t)INT32_MIN - 1;
    errno = 0;
    assert(rp2040_time32(NULL) == -1 && errno == EOVERFLOW);
    assert(rp2040_difftime32(INT32_MAX, INT32_MIN) == 4294967295.0);
    setenv("TZ", "UTC0", 1); tzset();
    rp2040_time32_t epoch = 0;
    struct tm tm;
    assert(rp2040_gmtime32_r(&epoch, &tm) == &tm);
    assert(tm.tm_year == 70 && tm.tm_mon == 0 && tm.tm_mday == 1);
    assert(rp2040_localtime32(&epoch)->tm_year == 70);
    assert(rp2040_mktime32(&tm) == 0);
    char date[26];
    assert(rp2040_ctime32_r(&epoch, date) == date);
    assert(strcmp(date, "Thu Jan  1 00:00:00 1970\n") == 0);
    tm.tm_year = 139; tm.tm_mon = 0; tm.tm_mday = 1;
    errno = 0;
    assert(rp2040_mktime32(&tm) == -1 && errno == EOVERFLOW);
    errno = 0;
    assert(rp2040_localtime32(NULL) == NULL && errno == EFAULT);
    assert(rp2040_gmtime32_r(&epoch, NULL) == NULL);
    assert(rp2040_ctime32_r(NULL, date) == NULL);
    return 0;
}
