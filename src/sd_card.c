/*
 * SD/SDHC card driver over SPI, for the engine's storage fallback.
 *
 * SPI mode, not SDIO - simpler and adequate for save files.
 *
 * Wiring (SPI1), the RC2040-style pinout:
 *     Pico pin 14 = SCK
 *     Pico pin 15 = MOSI
 *     Pico pin 12 = MISO
 *     Pico pin 13 = CS
 *
 * The SDK has no SD driver, so this is a minimal one: enough for
 * FatFs to mount a FAT12/16/32 volume and read/write files.
 *
 * Unlike the onboard-flash volume this path has no host-side equivalent to
 * check it against, and it is exercised only when the flash volume is absent,
 * so treat it as unproven until it has been run on real silicon. The command
 * and block protocol here follows the SPI-mode section of the physical-layer
 * specification; the CSD geometry decode (sd_read_csd_sectors) is the part
 * most in need of a look with an actual card's CSD bytes.
 */

#include <stdint.h>
#include <string.h>

#include <pico/stdlib.h>
#include <hardware/spi.h>
#include <hardware/gpio.h>

#include "sd_card.h"

/* --- wiring (SPI1, RC2040-style pinout) ---------------------------- */

#define SD_SPI       spi1
#define SD_PIN_SCK   14
#define SD_PIN_MOSI  15
#define SD_PIN_MISO  12
#define SD_PIN_CS    13

#define SD_SPEED_INIT 250000
#define SD_SPEED_FAST 8000000

/*
 * Command indices. The byte that actually goes on the wire is not the index:
 * an SPI-mode command frame starts with 0b01 followed by the six-bit index,
 * i.e. 0x40 | index. sd_cmd_r1() adds that, so callers pass the bare index.
 */
#define CMD_GO_IDLE          0
#define CMD_SEND_OP_COND     1
#define CMD_SEND_IF_COND     8
#define CMD_SEND_CSD         9
#define CMD_SET_BLOCKLEN     16
#define CMD_READ_BLOCK       17
#define CMD_WRITE_BLOCK      24
#define CMD_APP_OP_COND      41   /* ACMD41: only valid right after CMD55 */
#define CMD_APP_CMD          55   /* CMD55: marks the next command as an app cmd */
#define CMD_READ_OCR         58

#define R1_READY_STATE       0x00
#define R1_IDLE_STATE        0x01

/* Data token that introduces a read or write data block. */
#define TOKEN_START_BLOCK     0xFE

/* CMD8 check pattern: 2.7-3.6V (0x1) with test pattern 0xAA. */
#define CMD8_ARGS 0x000001AA

static bool sd_initialised = false;
static bool sd_high_capacity = false;   /* from OCR CCS: address by block, not byte */

/* --- low level SPI -------------------------------------------------- */

static void cs_low(void)  { gpio_put(SD_PIN_CS, false); }
static void cs_high(void) { gpio_put(SD_PIN_CS, true); }

static void sd_write_byte(uint8_t b)
{
    spi_write_blocking(SD_SPI, &b, 1);
}

static uint8_t sd_read_byte(void)
{
    /* Transmit 0xFF so the card drives MISO. */
    uint8_t rx = 0xFF;
    spi_read_blocking(SD_SPI, 0xFF, &rx, 1);
    return rx;
}

/*
 * Send a command frame and read the first response byte, with CS left exactly
 * as it was found - the caller is responsible for asserting it. Used for the
 * data commands (CMD17/CMD24/CMD9), which hold CS across the whole exchange,
 * and wrapped by sd_command() for the rest.
 */
static uint8_t sd_cmd_r1(uint8_t cmd, uint32_t arg)
{
    uint8_t crc;

    /*
     * In SPI mode only CMD0 and CMD8 have their CRC checked; the rest run
     * with checking disabled, so 0xFF is fine for them. CMD0 and CMD8 use
     * their fixed, documented checksums for the arguments passed here.
     */
    if (cmd == CMD_GO_IDLE)           crc = 0x95;
    else if (cmd == CMD_SEND_IF_COND) crc = 0x87;
    else                              crc = 0xFF;

    sd_write_byte(0x40 | cmd);
    sd_write_byte((arg >> 24) & 0xFF);
    sd_write_byte((arg >> 16) & 0xFF);
    sd_write_byte((arg >> 8) & 0xFF);
    sd_write_byte(arg & 0xFF);
    sd_write_byte(crc);

    /* R1: the card signals by pulling MISO low; 0xFF means "not answered yet". */
    uint8_t r = 0xFF;
    for (int i = 0; i < 8; i++) {
        r = sd_read_byte();
        if (r != 0xFF)
            break;
    }
    return r;
}

/* Send a command with CS asserted for the exchange and released after. */
static uint8_t sd_command(uint8_t cmd, uint32_t arg)
{
    cs_low();
    uint8_t r = sd_cmd_r1(cmd, arg);
    cs_high();
    return r;
}

/*
 * Wait for the card to leave its busy state. In SPI the card holds the busy
 * line (MISO) low while it writes and releases it high when done, so a 0xFF
 * read - not a non-0xFF read - is what "ready" means.
 */
static bool sd_wait_ready(uint32_t timeout_ms)
{
    absolute_time_t until = delayed_by_ms(get_absolute_time(), timeout_ms);
    bool ready = false;

    cs_low();
    while (absolute_time_diff_us(get_absolute_time(), until) > 0) {
        if (sd_read_byte() == 0xFF) {
            ready = true;
            break;
        }
    }
    cs_high();
    return ready;
}

/* Byte address for a data command: SDSC cards address by byte, SDHC by block. */
static uint32_t sd_block_addr(LBA_t sector)
{
    return sd_high_capacity ? (uint32_t)sector : (uint32_t)(sector * 512);
}

/* Read a single block at sector. Assumes the card is initialised. */
static bool sd_read_block(LBA_t sector, uint8_t *buf)
{
    cs_low();

    if (sd_cmd_r1(CMD_READ_BLOCK, sd_block_addr(sector)) != R1_READY_STATE) {
        cs_high();
        return false;
    }

    /*
     * The card returns 0xFF until the data is ready, then the start token
     * (0xFE) followed by 512 bytes and a 2-byte CRC. Anything else is an
     * error token.
     */
    uint8_t token;
    int guard = 0;
    do {
        token = sd_read_byte();
    } while (token == 0xFF && ++guard < 100000);

    if (token != TOKEN_START_BLOCK) {
        cs_high();
        return false;
    }

    for (int i = 0; i < 512; i++)
        buf[i] = sd_read_byte();

    (void)sd_read_byte();       /* data CRC, unchecked */
    (void)sd_read_byte();

    cs_high();
    return true;
}

static bool sd_write_block(LBA_t sector, const uint8_t *buf)
{
    cs_low();

    if (sd_cmd_r1(CMD_WRITE_BLOCK, sd_block_addr(sector)) != R1_READY_STATE) {
        cs_high();
        return false;
    }

    sd_write_byte(TOKEN_START_BLOCK);
    for (int i = 0; i < 512; i++)
        sd_write_byte(buf[i]);

    /*
     * Data CRC. With CRC checking off in SPI mode the card ignores these;
     * sending 0x00 is the documented choice for byte-at-a-time transfers.
     */
    sd_write_byte(0x00);
    sd_write_byte(0x00);

    /* Data-response token: low bits 0101 mean the card accepted the block. */
    uint8_t resp = sd_read_byte();
    cs_high();

    if ((resp & 0x1F) != 0x05)
        return false;

    /* Program the block; the card is busy on MISO until it finishes. */
    return sd_wait_ready(300);
}

/*
 * Read the card's CSD register (CMD9) and decode the capacity.
 *
 * The CSD layout differs by version:
 *   v1.0 (SDSC, <=2GB): capacity = (C_SIZE+1) * 2^(C_SIZE_MULT+2) * 2^READ_BL_LEN
 *   v2.0 (SDHC/SDXC):   capacity = (C_SIZE+1) * 512KB
 *
 * Returns the capacity in 512-byte sectors, or 0 if the CSD could not
 * be read or is a version we do not decode.
 */
static DWORD sd_read_csd_sectors(uint8_t *csd)
{
    uint8_t version;
    uint32_t c_size;
    uint32_t capacity_bytes;

    /* CMD9 answers R1 then the 16-byte CSD directly, so CS stays asserted. */
    cs_low();
    if (sd_cmd_r1(CMD_SEND_CSD, 0) == 0xFF) {   /* card did not answer */
        cs_high();
        return 0;
    }
    for (int i = 0; i < 16; i++)
        csd[i] = sd_read_byte();
    cs_high();
    sd_wait_ready(50);

    version = (uint8_t)(csd[0] >> 6);

    if (version == 0) {
        /*
         * CSD v1.0 (SDSC): up to 2GB.
         *   READ_BL_LEN  = csd[5] bits 7-4  (9 for 512-byte blocks)
         *   C_SIZE       = csd[6] low bits, high part of the 12-bit field
         *   C_SIZE_MULT  = csd[8] bits 2-0
         * Note the +2 in the exponent: DeviceSize carries MULT(2, C_SIZE_MULT+2).
         */
        uint32_t read_bl_len = (csd[5] >> 4) & 0x0F;
        uint32_t c_size_mult = csd[8] & 0x07;

        c_size = ((uint32_t)(csd[6] & 0x0F) << 8) | csd[7];

        capacity_bytes = (c_size + 1) << (c_size_mult + 2 + read_bl_len);
    } else if (version == 1) {
        /* CSD v2.0 (SDHC/SDXC): capacity is always a multiple of 512KB. */
        c_size = ((uint32_t)(csd[6] & 0x0F) << 16) |
                 ((uint32_t)csd[7] << 8) |
                 ((csd[8] >> 4) & 0x0F);

        capacity_bytes = (c_size + 1) << 19; /* 512KB */
    } else {
        /* v3.0 (SDUC) is not expected here. */
        return 0;
    }

    if (capacity_bytes == 0)
        return 0;

    return (DWORD)(capacity_bytes / 512);
}

/* --- init ----------------------------------------------------------- */

bool sd_init(void)
{
    uint8_t r;

    gpio_init(SD_PIN_SCK);
    gpio_init(SD_PIN_MOSI);
    gpio_init(SD_PIN_MISO);
    gpio_init(SD_PIN_CS);

    gpio_set_function(SD_PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(SD_PIN_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(SD_PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(SD_PIN_CS, GPIO_FUNC_SIO);
    gpio_set_dir(SD_PIN_CS, true);
    cs_high();

    spi_init(SD_SPI, SD_SPEED_INIT);
    spi_set_format(SD_SPI, 8, 0, 0, SPI_MSB_FIRST);

    /* 80 clocks with CS high puts the card into native SPI mode. */
    for (int i = 0; i < 80; i++)
        sd_write_byte(0xFF);

    /* CMD0: software reset. The card answers idle (0x01). */
    r = sd_command(CMD_GO_IDLE, 0);
    if (r != R1_IDLE_STATE) {
        r = sd_command(CMD_GO_IDLE, 0);   /* one retry for a slow card */
        if (r != R1_IDLE_STATE)
            return false;
    }

    /*
     * CMD8: voltage check. A v2 card echoes the check pattern in R7 and stays
     * idle; an older card rejects the command and stays idle too. Either way
     * we continue - the ACMD41 loop below settles the card either way.
     */
    sd_command(CMD_SEND_IF_COND, CMD8_ARGS);

    /* ACMD41 (CMD55 then CMD41) until the card leaves idle. HCS (bit 30)
     * asks a v2 card for block addressing. */
    {
        bool done = false;
        for (int i = 0; i < 1000 && !done; i++) {
            sd_command(CMD_APP_CMD, 0);
            r = sd_command(CMD_APP_OP_COND, 0x40000000u);
            done = (r == R1_READY_STATE);
            if (!done)
                sleep_ms(1);
        }
        if (!done)
            return false;
    }

    /*
     * CMD58: read the OCR. Bit 30 (CCS) says whether the card addresses by
     * block (SDHC/SDXC) or by byte (SDSC); every data command depends on it.
     * R3 carries no CRC to check.
     */
    cs_low();
    (void)sd_cmd_r1(CMD_READ_OCR, 0);
    uint8_t ocr_top = sd_read_byte();       /* OCR bits 31..24, CCS is bit 30 */
    (void)sd_read_byte();
    (void)sd_read_byte();
    (void)sd_read_byte();
    cs_high();
    sd_high_capacity = (ocr_top & 0x40) != 0;

    /* Fix the block length at 512 (meaningful for SDSC; ignored by SDHC). */
    sd_command(CMD_SET_BLOCKLEN, 512);
    sd_wait_ready(100);

    /* Leave the card deselected and go fast. */
    cs_high();
    spi_set_baudrate(SD_SPI, SD_SPEED_FAST);

    sd_initialised = true;
    return true;
}

bool sd_ready(void)
{
    return sd_initialised;
}

/* --- FatFs disk I/O layer ------------------------------------------- */

DWORD sd_get_sector_count(void)
{
    uint8_t csd[16];

    if (!sd_initialised)
        return 0;

    return sd_read_csd_sectors(csd);
}

DRESULT sd_disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
    (void)pdrv;

    if (!sd_initialised)
        return RES_NOTRDY;

    while (count--) {
        if (!sd_read_block(sector, buff))
            return RES_ERROR;
        buff += 512;
        sector++;
    }
    return RES_OK;
}

DRESULT sd_disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
    (void)pdrv;

    if (!sd_initialised)
        return RES_NOTRDY;

    while (count--) {
        if (!sd_write_block(sector, buff))
            return RES_ERROR;
        buff += 512;
        sector++;
    }
    return RES_OK;
}

DRESULT sd_disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
    (void)pdrv;

    switch (cmd) {
    case CTRL_SYNC:
        return RES_OK;
    case GET_SECTOR_COUNT:
        *(DWORD *)buff = sd_get_sector_count();
        return RES_OK;
    case GET_SECTOR_SIZE:
        *(WORD *)buff = 512;
        return RES_OK;
    case GET_BLOCK_SIZE:
        /* Erase granularity; we never erase, so 1 is honest. */
        *(DWORD *)buff = 1;
        return RES_OK;
    default:
        return RES_PARERR;
    }
}
