#ifndef RP2040_CONSOLE_SD_CARD_H
#define RP2040_CONSOLE_SD_CARD_H

#include <stdbool.h>
#include <stdint.h>

/*
 * Types and status codes come from FatFs rather than being redeclared
 * here, so the DRESULT values this driver returns are exactly the ones
 * FatFs compares against. ff.h defines the integer types that diskio.h
 * uses, so it has to come first.
 */
#include "ff.h"
#include "diskio.h"

bool     sd_init(void);
bool     sd_ready(void);

DWORD    sd_get_sector_count(void);
DRESULT  sd_disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count);
DRESULT  sd_disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count);
DRESULT  sd_disk_ioctl(BYTE pdrv, BYTE cmd, void *buff);

#endif /* RP2040_CONSOLE_SD_CARD_H */