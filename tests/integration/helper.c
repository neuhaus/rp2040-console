#include "termcap.h"
int verify_time_abi(void);
int panic(int value) { return value + verify_time_abi(); }
