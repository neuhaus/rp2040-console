#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <locale.h>
#include <time.h>
#include <pico/stdlib.h>
#include <hardware/watchdog.h>
#include "soft_reboot_internal.h"
#include "console.h"
#include "env.h"
#include "storage.h"

extern unsigned char __rp2040_program_data_start[], __rp2040_program_data_end[];
extern unsigned char __rp2040_program_data_load[];
extern unsigned char __rp2040_program_bss_start[], __rp2040_program_bss_end[];
void console_process_reset(void);
void console_files_begin_session(void);
int console_files_close_session(void);

void rp2040_soft_restore(void)
{
    memcpy(__rp2040_program_data_start, __rp2040_program_data_load,
           (uintptr_t)__rp2040_program_data_end - (uintptr_t)__rp2040_program_data_start);
    memset(__rp2040_program_bss_start, 0,
           (uintptr_t)__rp2040_program_bss_end - (uintptr_t)__rp2040_program_bss_start);
    env_reset();
    console_process_reset();
    console_files_begin_session();
    console_reset_terminal();
    console_rawout("\033(B\033>");
    console_discard_input();
}

void rp2040_soft_platform_cleanup(void)
{
    /* Report/stop on a failed close instead of starting over unsynced files. */
    if (console_files_close_session() != 0) rp2040_soft_fatal(127);
    if (storage_is_available() && chdir("/") != 0) rp2040_soft_fatal(127);
    env_reset();
    tzset();
    setlocale(LC_ALL, "C");
}

void rp2040_soft_fatal(int status)
{
    printf("\nSoft reboot cleanup failed (%d); rebooting hardware.\n", status);
    watchdog_reboot(0, 0, 0);
    for (;;) tight_loop_contents();
}
