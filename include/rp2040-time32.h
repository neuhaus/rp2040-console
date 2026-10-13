#ifndef RP2040_CONSOLE_TIME32_H
#define RP2040_CONSOLE_TIME32_H

#include <stdint.h>
#include <time.h>

/* Signed Unix seconds, with the legacy 1901..2038 range. Native libc
 * declarations and struct layouts retain their actual time_t ABI. */
typedef int32_t rp2040_time32_t;
typedef time_t rp2040_native_time_t;

#ifdef __cplusplus
extern "C" {
#endif

rp2040_time32_t rp2040_time32(rp2040_time32_t *out);
struct tm *rp2040_localtime32(const rp2040_time32_t *value);
struct tm *rp2040_gmtime32(const rp2040_time32_t *value);
struct tm *rp2040_localtime32_r(const rp2040_time32_t *value, struct tm *out);
struct tm *rp2040_gmtime32_r(const rp2040_time32_t *value, struct tm *out);
char *rp2040_ctime32(const rp2040_time32_t *value);
char *rp2040_ctime32_r(const rp2040_time32_t *value, char *out);
rp2040_time32_t rp2040_mktime32(struct tm *value);
double rp2040_difftime32(rp2040_time32_t end, rp2040_time32_t start);

#ifdef __cplusplus
}
#endif

#endif
