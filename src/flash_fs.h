#ifndef RP2040_CONSOLE_FLASH_FS_H
#define RP2040_CONSOLE_FLASH_FS_H

#include <stdint.h>

#include "ff.h"
#include "diskio.h"

/*
 * Onboard-flash FAT volume, presented to FatFs as drive 0.
 *
 * The game needs a filesystem for its lock files, data files, records,
 * saves and bones. Carving a FAT volume out of the Pico's own flash
 * means it works with no SD card, no wiring and no CSD parsing.
 *
 * Layout: 512KB at flash offset 0x180000. The firmware occupies roughly
 * 300KB, so there is no overlap.
 */
DWORD   get_flash_sector_count(void);

DRESULT flash_disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count);
DRESULT flash_disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count);
DRESULT flash_disk_ioctl(BYTE pdrv, BYTE cmd, void *buff);

#endif /* RP2040_CONSOLE_FLASH_FS_H */