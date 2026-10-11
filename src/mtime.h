#ifndef RP2040_CONSOLE_MTIME_H
#define RP2040_CONSOLE_MTIME_H

#include <time.h>

/*
 * Remembered modification times, in the folded time_t that stat() reports.
 *
 * FatFs here cannot date files: it is built timestamp-off, so everything the
 * device writes is stamped 2022 while the volume's own files carry real
 * dates, and anything comparing the two - a saved level against the installed
 * game - concludes the new file is older. This table carries the session
 * clock instead: the date entered at startup, as it stood when the file was
 * closed. It lives in a dot-file on the volume, so it survives the reboot
 * that the save itself has to survive.
 *
 * Only files the engine wrote appear here. Anything else keeps its directory
 * time.
 */

time_t mtime_lookup(const char *path, int *found);
void mtime_store(const char *path, time_t t);
void mtime_move(const char *oldpath, const char *newpath);
void mtime_drop(const char *path);

#endif /* RP2040_CONSOLE_MTIME_H */
