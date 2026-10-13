#include <time.h>
#include <sys/time.h>
#ifdef RP2040_CONSOLE_LEGACY_TIME32
_Static_assert(sizeof(time_t) == 4, "legacy option must apply to this target");
#else
_Static_assert(sizeof(time_t) == 8, "legacy option must not leak between targets");
#endif
_Static_assert(sizeof(((struct timeval *)0)->tv_sec) == 8, "system struct ABI must remain native");
int verify_time_abi(void) { return 0; }
