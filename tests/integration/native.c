#include <time.h>
_Static_assert(sizeof(time_t) == 8, "native newlib ABI must remain intact");
int verify_time_abi(void);
int main(void) { time_t t; time(&t); return verify_time_abi(); }
