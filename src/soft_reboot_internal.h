#ifndef RP2040_CONSOLE_SOFT_REBOOT_INTERNAL_H
#define RP2040_CONSOLE_SOFT_REBOOT_INTERNAL_H

#include <stddef.h>
#include <reent.h>

extern struct _reent rp2040_program_reent;

/* Runtime hooks implemented by the Pico backend (and the executable tests). */
void rp2040_soft_restore(void);
int rp2040_soft_libc_begin(void);
int rp2040_soft_libc_end(int flush);
void rp2040_soft_platform_cleanup(void);
void rp2040_soft_fatal(int status) __attribute__((noreturn));
int rp2040_soft_active(void);
size_t rp2040_soft_live_allocations(void);

#endif
