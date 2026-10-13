#ifndef RP2040_CONSOLE_STORAGE_H
#define RP2040_CONSOLE_STORAGE_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Mount the SD card so the game can read data files and write saves.
 *
 * Must be called before the game's main() does any file I/O. Returns
 * false if the card is absent or unformatted; the game can still run
 * read-only in that case, just not save.
 */
bool storage_mount(void);

/*
 * Tell the POSIX file layer whether a filesystem is actually mounted.
 * When false, open/stat/access report ENOENT for everything, because
 * callers treat ENOENT as "carry on" and anything else as fatal.
 */
void storage_set_available(bool available);

/* Whether storage_mount() succeeded; file calls fail safe without it. */
bool storage_is_available(void);

/*
 * Free space on the mounted volume, in bytes.
 *
 * Returns false when no volume is mounted or FatFs cannot answer the query,
 * in which case *bytes is left untouched. This is the honest "how much is
 * left" a game that writes levels or saves to a small volume wants to show
 * when it ends.
 */
bool storage_free_bytes(uint64_t *bytes);

#endif /* RP2040_CONSOLE_STORAGE_H */
