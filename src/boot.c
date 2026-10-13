/*
 * Startup report: a one-line banner, then (optionally) the saved games.
 *
 * The banner is console chrome any game can have: its text comes from the
 * build-time environment the application declares (BANNER). It runs before
 * the game's first screen and does not clear anything, so a terminal keeps
 * the line in its scrollback after the game draws over it.
 *
 * The saved-game listing is not general - which directory holds "saves", and
 * that they are worth listing, is a fact about one game - so it is compiled
 * only when the manifest sets LISTDIR, and that name is the directory.
 *
 * "Last saved" cannot come from the directory entry: FatFs is built
 * timestamp-off and stamps every file it writes 2022. The engine's mtime
 * table remembers the session clock per file, so that is the source, and a
 * file it never saw shows without a date.
 */

#include <stdio.h>
#include <stdlib.h>

#include "storage.h"
#include "boot.h"

void
boot_report(void)
{
    const char *banner = getenv("BANNER");

    if (banner != NULL && banner[0] != '\0')
        printf("\n%s\n", banner);

#ifdef RP2040_CONSOLE_LISTDIR
    boot_list_dir(RP2040_CONSOLE_LISTDIR);
#endif
}

#ifdef RP2040_CONSOLE_LISTDIR

#include <time.h>

#include "ff.h"
#include "mtime.h"

void
boot_list_dir(const char *dir)
{
    DIR d;
    FILINFO fi;
    FRESULT fr;
    unsigned count = 0;

    if (!storage_is_available())
        return;

    fr = f_opendir(&d, dir);
    if (fr != FR_OK) {
        printf("Cannot open the %s directory (%d).\n", dir, (int) fr);
        return;
    }

    printf("Saved games in %s/:\n", dir);
    for (;;) {
        char path[64];
        int found;
        time_t epoch;
        struct tm *tm;

        fr = f_readdir(&d, &fi);
        if (fr != FR_OK || fi.fname[0] == '\0')
            break;
        if ((fi.fattrib & AM_DIR) || fi.fname[0] == '.')
            continue;

        snprintf(path, sizeof path, "%s/%s", dir, fi.fname);
        epoch = mtime_epoch(path, &found);
        tm = found ? gmtime(&epoch) : NULL;
        if (tm != NULL)
            printf("  %-20s %8lu bytes  %04d-%02d-%02d %02d:%02d\n",
                   fi.fname, (unsigned long) fi.fsize,
                   tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday,
                   tm->tm_hour, tm->tm_min);
        else
            printf("  %-20s %8lu bytes\n",
                   fi.fname, (unsigned long) fi.fsize);
        count++;
    }
    f_closedir(&d);

    if (count == 0)
        printf("  (none)\n");
}

#endif /* RP2040_CONSOLE_LISTDIR */
