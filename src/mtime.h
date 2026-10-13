#ifndef RP2040_CONSOLE_MTIME_H
#define RP2040_CONSOLE_MTIME_H

#include <time.h>

/*
 * Remembered modification times.
 *
 * FatFs here cannot date files: it is built timestamp-off, so everything the
 * device writes is stamped 2022 while the volume's own files carry real
 * dates, and anything comparing the two - a saved level against the installed
 * game - concludes the new file is older. This table carries the session
 * clock instead: the date entered at startup, as it stood when the file was
 * closed. It lives in a dot-file on the volume, so it survives the reboot
 * that the save itself has to survive.
 *
 * Each entry holds two things: the folded time_t that stat() reports, because
 * every source has to fold the same way to stay comparable, and the real
 * epoch, which is what a "last saved" listing wants to show. Only files the
 * engine wrote appear here. Anything else keeps its directory time.
 */

time_t mtime_lookup(const char *path, int *found);

/*
 * The real Unix time recorded for path, for display rather than comparison.
 * Returns 0 and sets *found to 0 when there is no entry.
 */
time_t mtime_epoch(const char *path, int *found);

void mtime_store(const char *path, time_t mtime, time_t epoch);
void mtime_move(const char *oldpath, const char *newpath);
void mtime_drop(const char *path);

#endif /* RP2040_CONSOLE_MTIME_H */
