#ifndef RP2040_CONSOLE_BOOT_H
#define RP2040_CONSOLE_BOOT_H

/*
 * Print the startup banner, and the saved-game listing when the manifest
 * asked for one. Called once by the engine before the game runs. Nothing
 * here clears the screen, so the lines stay in the terminal's scrollback.
 */
void boot_report(void);

#ifdef RP2040_CONSOLE_LISTDIR
/* List a directory on the mounted volume; see boot.c. */
void boot_list_dir(const char *dir);
#endif

#endif /* RP2040_CONSOLE_BOOT_H */
