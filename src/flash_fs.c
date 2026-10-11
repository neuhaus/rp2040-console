/*
 * Onboard-flash block device for FatFs.
 *
 * The game needs a real filesystem: it locks with "perm"/"game.lock"/
 * "record.lock", reads data/help/hh/rumors, and writes "record", save
 * files and bones. Without one it calls error() and exits, which halts
 * the board before USB finishes enumerating.
 *
 * Rather than requiring an SD card, carve a FAT volume out of the
 * Pico's own 2MB of QSPI flash and hand it to FatFs through the normal
 * disk_io interface. The game is then self-contained: no card, no
 * wiring, no CSD parsing.
 *
 * The volume itself is built on the host by mkfilesystem.sh and flashed
 * as a separate UF2 at RP2040_CONSOLE_FS_OFFSET. So there is no f_mkfs on the
 * device, and - the useful part - the image is verified with fsck.vfat
 * before it ever reaches the board.
 *
 * Layout (RP2040 has 2MB):
 *
 *   0x000000 .. program + firmware (about 300KB, ends near 0x4A900)
 *   0x180000 .. 0x1FFFFF   the FAT volume, 512KB
 *
 *
 * ERASE GRANULARITY IS THE WHOLE STORY HERE
 *
 * The Pico's flash erases in 4096-byte blocks but programs in 256-byte
 * pages, and NOR flash can only *clear* bits. FatFs, meanwhile, thinks in
 * 512-byte sectors and (as mkfs.vfat chooses for a 512KB volume) 2048-byte
 * clusters.
 *
 * That means one 512-byte FatFs sector is only an eighth of an erase block,
 * and an erase block holds two clusters. So writing one sector requires a
 * read-modify-erase-write of the *entire* 4096-byte block: erase first and
 * the other seven sectors are gone, silently corrupting whichever cluster
 * happened to sit next door. An earlier version of this file erased 4096
 * and reprogrammed only 512, which destroyed a neighbouring cluster on
 * every single write.
 *
 * Hence block_buf: the whole erase block is read into RAM, the one sector
 * is patched in, and then the block is erased and reprogrammed wholesale.
 *
 *
 * INTERRUPTS MUST BE OFF
 *
 * flash_range_erase()/flash_range_program() are unsafe if any interrupt
 * handler or vector table lives in flash - which is exactly our case, as
 * the firmware is built with PICO_COPY_TO_RAM 0 and executes from XIP. An
 * IRQ arriving mid-erase would vector into flash that is mid-erase and
 * fault. The SDK's own header says so explicitly. We run with a live USB
 * interrupt, so save_and_disable_interrupts() brackets the sequence.
 */

#include <stdio.h>
#include <string.h>

#include <hardware/flash.h>
#include <hardware/sync.h>
#include <pico/platform.h>
#include <pico/stdlib.h>
#include "trace.h"

#include "ff.h"
#include "diskio.h"

/*
 * Where the volume lives in flash, and how big it is.
 *
 * Both come from the build: the game's manifest sets them and the same
 * values are handed to mkvolume.sh, so the driver and the image it mounts
 * cannot drift apart. The defaults are the last 512KB of the Pico's 2MB
 * chip - 0x200000 - 0x80000 = 0x180000 - which leaves a wide gap between
 * the program and the volume.
 *
 * The arithmetic is worth stating rather than assuming: an offset whose
 * size runs past the end of the chip leaves part of the volume with no
 * flash behind it, and nothing complains at mount time. mkvolume.sh checks
 * for exactly that before emitting anything.
 */
#ifndef RP2040_CONSOLE_FS_OFFSET
#define RP2040_CONSOLE_FS_OFFSET 0x180000u
#endif

#ifndef RP2040_CONSOLE_FS_SIZE
#define RP2040_CONSOLE_FS_SIZE (512u * 1024u)
#endif

#define FS_SIZE_SECTORS (RP2040_CONSOLE_FS_SIZE / 512u)

#define FS_SECTOR_SIZE  512u
#define FS_BLOCK_SIZE   FLASH_SECTOR_SIZE       /* 4096: the erase unit */
#define FS_SECTORS_PER_BLOCK (FS_BLOCK_SIZE / FS_SECTOR_SIZE)
#define FS_PAGES_PER_BLOCK   (FS_BLOCK_SIZE / FLASH_PAGE_SIZE)

/*
 * One erase block, staged in RAM for read-modify-write. This is the only
 * significant RAM cost of the whole filesystem driver.
 */
static uint8_t block_buf[FS_BLOCK_SIZE];

/* --- flash offset vs XIP address -------------------------------------
 *
 * Two different numbers describe the same bytes, and confusing them is
 * the single easiest mistake to make here:
 *
 *   RP2040_CONSOLE_FS_OFFSET  a *flash offset* - what flash_range_erase() and
 *                 flash_range_program() take. 0x180000 means "1.5MB
 *                 into the chip".
 *
 *   FS_FLASH_XIP   the *memory-mapped address* - what a pointer
 *                 dereference needs. Flash is visible at XIP_BASE
 *                 (0x10000000), so 0x180000 into the chip is read at
 *                 0x10180000, not at 0x180000.
 *
 * Passing the bare offset to memcpy() reads 0x180000, which is not flash
 * at all - it is below the XIP window and nothing is mapped there. That
 * did exactly what it sounds like: garbage boot sectors, and a board
 * that reset in a loop inside f_mount(), showing only probe [3] [4]
 * over and over.
 *
 * So: offsets for the flash API, XIP addresses for pointers. The two
 * names above exist to keep them apart.
 */
#define FS_FLASH_XIP (XIP_BASE + RP2040_CONSOLE_FS_OFFSET)

/* --- flash helpers -------------------------------------------------- */

/*
 * Reads are plain memory-mapped XIP. The SDK invalidates the XIP cache on
 * erase and program, so data written this boot reads back correctly
 * without any cache management here.
 */
static void flash_read_sectors(LBA_t sector, uint8_t *dst, UINT count)
{
    memcpy(dst,
           (const void *)(uintptr_t)(FS_FLASH_XIP + sector * FS_SECTOR_SIZE),
           (size_t)count * FS_SECTOR_SIZE);
}

/*
 * Erase and reprogram one 512-byte sector.
 *
 * The caller must already have staged the full erase block, with this
 * sector's contents patched in, in block_buf.
 */
static void flash_program_block(uint32_t block_addr, const uint8_t *src)
{
    uint32_t irq = save_and_disable_interrupts();

    flash_range_erase(block_addr, FS_BLOCK_SIZE);

    /*
     * flash_range_program() needs a page-aligned offset and a
     * page-multiple length. 4096 is 16 pages of 256, and the volume base
     * is page-aligned, so this walks cleanly.
     */
    for (uint i = 0; i < FS_PAGES_PER_BLOCK; i++)
        flash_range_program(block_addr + i * FLASH_PAGE_SIZE,
                            src + i * FLASH_PAGE_SIZE,
                            FLASH_PAGE_SIZE);

    restore_interrupts(irq);
}

/* --- FatFs disk interface ------------------------------------------- */

/*
 * Drive 0 is the onboard flash, which always exists and needs no card.
 *
 * It has to be drive 0. The game opens bare relative paths - "game.lock",
 * "record", "data" - with no "N:" prefix, and get_ldnumber() resolves a
 * prefix-less path to the current drive (FF_FS_RPATH), which is 0 and
 * cannot be changed at runtime: this FatFs has no f_chdrv(). A volume on
 * any other drive is unreachable by exactly the paths the game uses, even
 * though mounting it succeeds, so nothing here appears to be wrong.
 *
 * An SD card is a fallback rather than a companion: storage_mount() uses
 * the flash volume when it is present and only brings up the card - on the
 * same current drive - when it is not.
 */

DWORD get_flash_sector_count(void)
{
    return FS_SIZE_SECTORS;
}

DRESULT flash_disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv != 0)
        return RES_PARERR;

    flash_read_sectors(sector, buff, count);
    return RES_OK;
}

DRESULT flash_disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    if (pdrv != 0)
        return RES_PARERR;

    /*
     * The first write is the interesting one when a volume will not take
     * changes: it says whether FatFs reached us at all, and what it asked
     * for. Everything after that is routine, and routine does not belong on
     * the player's screen.
     */
    static bool reported;
    if (!reported) {
        reported = true;
        TRACE("flash_fs: first write pdrv=%u sector=%lu count=%u "
              "(block=%lu off=%u)\n",
              pdrv, (unsigned long)sector, count,
              (unsigned long)(sector / FS_SECTORS_PER_BLOCK),
              (unsigned)(sector % FS_SECTORS_PER_BLOCK));
    }

    /*
     * Group the writes by erase block where possible. A single write
     * touching n sectors costs one erase per block it spans; this matters
     * because each erase is a full block-rewrite.
     */
    while (count) {
        uint32_t block     = (uint32_t)(sector / FS_SECTORS_PER_BLOCK);
        uint32_t offset    = (uint32_t)(sector % FS_SECTORS_PER_BLOCK);
        uint32_t in_block  = FS_SECTORS_PER_BLOCK - offset;
        UINT     n         = (count < in_block) ? count : in_block;
        uint32_t block_addr = RP2040_CONSOLE_FS_OFFSET + block * FS_BLOCK_SIZE;

        /*
         * Read the block BEFORE erasing it. After the erase there is
         * nothing left to read, so this copy is what preserves the
         * neighbouring sectors - and therefore the neighbouring cluster.
         *
         * XIP address for the read, flash offset for the erase/program
         * below: two different number spaces, deliberately named
         * differently at the top of this file.
         */
        memcpy(block_buf,
               (const void *)(uintptr_t)(FS_FLASH_XIP + block * FS_BLOCK_SIZE),
               FS_BLOCK_SIZE);

        memcpy(block_buf + offset * FS_SECTOR_SIZE, buff,
               (size_t)n * FS_SECTOR_SIZE);

        flash_program_block(block_addr, block_buf);

        buff   += (size_t)n * FS_SECTOR_SIZE;
        sector += n;
        count  -= n;
    }

    return RES_OK;
}

DRESULT flash_disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    if (pdrv != 0)
        return RES_PARERR;

    switch (cmd) {
    case CTRL_SYNC:
        /* Writes are already synchronous - flash_range_program blocks. */
        return RES_OK;
    case GET_SECTOR_COUNT:
        *(DWORD *)buff = FS_SIZE_SECTORS;
        return RES_OK;
    case GET_SECTOR_SIZE:
        *(WORD *)buff = FS_SECTOR_SIZE;
        return RES_OK;
    case GET_BLOCK_SIZE:
        /*
         * Reported honestly as the erase granularity. It is 8 sectors,
         * not 1, because a sector cannot be rewritten without rewriting
         * the whole block it lives in.
         */
        *(DWORD *)buff = FS_SECTORS_PER_BLOCK;
        return RES_OK;
    default:
        return RES_PARERR;
    }
}
