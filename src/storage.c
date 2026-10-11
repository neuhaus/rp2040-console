/*
 * Storage for a terminal game on RP2040: FatFs over the choices available.
 *
 * FatFs itself is BSD-licensed (ChaN) and is vendored in the Pico SDK
 * under lib/tinyusb/lib/fatfs. It is included rather than written here
 * because FAT is exactly the kind of code not to improvise: a
 * hand-rolled implementation would risk corrupting save files.
 *
 * This file is only the glue - it forwards FatFs's three disk entry
 * points to the SPI SD driver in sd_card.c and mounts the volume.
 */

#include <string.h>
#include <stdio.h>
#include <stdbool.h>

#include <pico/stdlib.h>
#include <hardware/sync.h>

#include "ff.h"
#include "diskio.h"
#include "sd_card.h"
#include "flash_fs.h"
#include "storage.h"
#include "trace.h"

FATFS   fs;
FIL     file;
DIR     dir;

/* --- FatFs disk interface ------------------------------------------- */

/*
 * Signatures here must match diskio.h exactly:
 *   DSTATUS disk_status(BYTE)
 *   DRESULT disk_read/disk_write(BYTE, ..., LBA_t, UINT)
 *   DRESULT disk_ioctl(BYTE, BYTE, void*)
 */

DSTATUS disk_status(BYTE pdrv)
{
    if (pdrv == 0)
        return 0;               /* onboard flash is always present */

    /*
     * A card present but mounted read-write is STA_NOINIT only until
     * FatFs has finished mounting; sd_ready() reflects our own init.
     */
    return sd_ready() ? 0 : STA_NOINIT;
}

DSTATUS disk_initialize(BYTE pdrv)
{
    if (pdrv == 0)
        return 0;

    return sd_ready() ? 0 : STA_NOINIT;
}

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    if (pdrv == 0)
        return flash_disk_ioctl(pdrv, cmd, buff);

    return sd_disk_ioctl(pdrv, cmd, buff);
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv == 0)
        return flash_disk_read(pdrv, buff, sector, count);

    /* FatFs owns the SPI bus; nothing else may touch it mid-transaction. */
    uint32_t save = save_and_disable_interrupts();
    DRESULT r = sd_disk_read(pdrv, buff, sector, count);
    restore_interrupts(save);

    return r;
}

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv == 0)
        return flash_disk_write(pdrv, buff, sector, count);

    uint32_t save = save_and_disable_interrupts();
    DRESULT r = sd_disk_write(pdrv, buff, sector, count);
    restore_interrupts(save);

    return r;
}

/* --- mount ----------------------------------------------------------- */

/*
 * Bring up the card and mount the first volume.
 *
 * Returns true on success. On failure a message is printed explaining
 * what went wrong, because a missing card is the expected first-boot
 * state and the player needs to know what to do about it.
 */
/*
 * Mount the prebuilt FAT volume on onboard flash.
 *
 * The filesystem is built on the host (see mkfilesystem.sh) and flashed
 * as a separate UF2 at 0x180000. That means no f_mkfs on the device, no
 * formatting at boot, and - the useful part - the image can be checked
 * with fsck.vfat before it ever reaches the board.
 */
static bool flash_mount(void)
{
    /*
     * Report what is actually in sector 0 before handing anything to
     * FatFs.
     *
     * Two questions get conflated otherwise. "Did filesystem.uf2 land?"
     * and "did FatFs like it?" have very different fixes - re-flashing
     * versus a rebuild - but f_mount() reports both as one failure code.
     *
     * A FAT boot sector carries several independent identifiers, and
     * printing all of them names whatever is actually sitting at
     * 0x180000 rather than just "not a boot sector":
     *
     *   0x000 jmp        0x55,0xAA at 510   boot signature
     *   3..10  OEM name  ("mkfs.fat")       who made it
     *   54..61 FAT type  ("FAT12   ")       what it is
     *
     * The signature bytes are 0x55 then 0xAA, so read as a little-endian
     * 16-bit word the value is 0xAA55 - not 0x55AA. Comparing against
     * 0x55AA rejects every genuine boot sector, which is exactly the
     * mistake this check existed to catch.
     *
     * Unwritten NOR flash reads 0xFF, so the cases stay distinct:
     *
     *   aa55 + "FAT12"   the image is there; later failures are FatFs's
     *   ffff             nothing written - filesystem.uf2 did not land
     *   anything else    stale image, or the volume moved
     */
    BYTE boot[512];
    flash_disk_read(0, boot, 0, 1);

    unsigned sig = (unsigned)boot[510] | ((unsigned)boot[511] << 8);

    TRACE("storage: flash sector0: sig=%04x oem=", sig);
    for (int i = 3; i < 11; i++)
        TRACE("%c", (boot[i] >= 32 && boot[i] < 127) ? boot[i] : '.');
    TRACE(" fstype=");
    for (int i = 54; i < 62; i++)
        TRACE("%c", (boot[i] >= 32 && boot[i] < 127) ? boot[i] : '.');
    TRACE("\n");

    if (sig == 0xFFFF) {
        printf("storage: blank flash - the volume image did not land\n");
        return false;
    }
    if (sig != 0xAA55) {
        printf("storage: not a FAT boot sector at 0x180000\n");
        return false;
    }

    /*
     * Mount as "0:", not "1:".
     *
     * The game uses bare relative paths ("game.lock", "record", "data").
     * get_ldnumber() routes a path with no "N:" prefix to the current
     * drive, which is 0, and this FatFs configuration has no f_chdrv() to
     * relocate it. A volume mounted on any other drive is therefore
     * invisible to every path the game uses, while f_mount() on it still
     * succeeds - so the drive number has to match, not merely be present.
     */
    FRESULT fr = f_mount(&fs, "0:", 1);

    if (fr != FR_OK) {
        printf("storage: mount failed (%d)\n", fr);
        return false;
    }

    printf("storage: mounted the onboard flash volume\n");
    return true;
}

bool storage_mount(void)
{
    /*
     * Onboard flash first. It always exists, needs no card and no
     * wiring, and it is what makes the game's lock files, data files
     * and saves work. It occupies drive 0 because that is the drive the
     * game's relative paths resolve to; an SD card, if fitted, takes
     * drive 1 and is not required.
     */
    if (flash_mount()) {
        storage_set_available(true);
        return true;
    }

    if (!sd_init())
    {
        printf("storage: SD init failed - check wiring and card\n");
        storage_set_available(false);
        return false;
    }

    if (sd_get_sector_count() == 0)
    {
        printf("storage: cannot determine card size (SDSC CSD not read)\n");
        storage_set_available(false);
        return false;
    }

    FRESULT fr = f_mount(&fs, "", 1);
    if (fr != FR_OK)
    {
        printf("storage: mount failed (%d) - is the card FAT formatted?\n", fr);
        storage_set_available(false);
        return false;
    }

    storage_set_available(true);
    return true;
}