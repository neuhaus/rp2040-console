#ifndef RP2040_CONSOLE_SOFT_REBOOT_NEWLIB_H
#define RP2040_CONSOLE_SOFT_REBOOT_NEWLIB_H

#include <reent.h>

/* The program's own reentrancy context, distinct from _GLOBAL_REENT. Every
 * newlib allocation belongs to one of them, which is what lets a session
 * boundary tell program objects and libc buffers apart from the platform's. */
extern struct _reent rp2040_program_reent;

#endif
