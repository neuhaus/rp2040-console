#ifndef RP2040_CONSOLE_SOFT_REBOOT_H
#define RP2040_CONSOLE_SOFT_REBOOT_H

/* Program-only exit interception; the engine and SDK retain native names. */
int rp2040_soft_run(int argc, char **argv);
int rp2040_soft_atexit(void (*handler)(void));
void rp2040_soft_exit(int status) __attribute__((noreturn));
void rp2040_soft_immediate_exit(int status) __attribute__((noreturn));

#endif
