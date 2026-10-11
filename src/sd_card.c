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

/* SPI command indices (CMD<n> = n | 0x40) */
#define CMD_GO_IDLE          0
#define CMD_SEND_OP_COND     1
#define CMD_SEND_RE_INIT     2
#define CMD_SEND_IF_COND     3
#define CMD_READ_BLOCK       17
#define CMD_WRITE_BLOCK      24
#define CMD_SEND_CSD         9
#define CMD_SELECT_CARD      55
#define CMD_APP_OP_COND      41
#define CMD_SET_BLOCKLEN     16

#define R1_READY_STATE       0x00
#define R1_IDLE_STATE        0x01

/* Data tokens */
#define TOKEN_START_BLOCK     0xFE
#define TOKEN_STOP_TRANSMIT   0xFD

#define CMD0_ARGS 0x00009595
#define CMD8_ARGS 0x000001AA

static bool sd_initialised = false;
static bool sd_is_spi_mode = false;

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

static uint8_t sd_command(uint8_t cmd, uint32_t arg)
{
    uint8_t crc = 0x00;
    uint8_t r;

    /* SPI mode needs CRC7 for CMD0/CMD8; 0x95 is the documented value. */
    if (cmd == CMD_GO_IDLE) {
        crc = 0x95;
    } else if (cmd == CMD_SEND_RE_INIT || cmd == CMD_SEND_OP_COND) {
        crc = 0xFF;
    }

    cs_low();

    sd_write_byte(cmd);
    sd_write_byte((arg >> 24) & 0xFF);
    sd_write_byte((arg >> 16) & 0xFF);
    sd_write_byte((arg >> 8) & 0xFF);
    sd_write_byte(arg & 0xFF);
    sd_write_byte(crc);

    /* R1 response: 8 bits (or 48 bits for CMD8). */
    for (int i = 0; i < 8; i++) {
        r = sd_read_byte();
        if (r != 0xFF)
            break;
    }

    cs_high();
    return r;
}

/* Wait for a card to stop being busy. Returns true on success. */
static bool sd_wait_ready(uint32_t timeout_ms)
{
    absolute_time_t until = delayed_by_ms(get_absolute_time(), timeout_ms);

    while (absolute_time_diff_us(get_absolute_time(), until) > 0) {
        if (sd_read_byte() != 0xFF)
            return true;
    }
    return false;
}

/* Read a single block into buf. Assumes card is in SPI mode. */
static bool sd_read_block(uint8_t *buf)
{
    cs_low();

    sd_write_byte(0xFF);
    sd_write_byte(TOKEN_START_BLOCK);

    /* Discard the data header CRC (2 bytes). */
    (void)sd_read_byte();
    (void)sd_read_byte();

    for (int i = 0; i < 512; i++)
        buf[i] = sd_read_byte();

    /* The final two bytes are the data CRC; ignore CRC16 verification. */
    (void)sd_read_byte();
    (void)sd_read_byte();

    cs_high();

    return true;
}

/*
 * Read the card's CSD register (CMD9) and decode the capacity.
 *
 * The CSD layout differs by version:
 *   v1.0 (SDSC, <=2GB): capacity = (C_SIZE+1) * 2^C_SIZE_MULT * 2^READ_BL_LEN
 *   v2.0 (SDHC/SDXC):   capacity = (C_SIZE+1) * 512KB
 *
 * Getting this right is what lets the card mount at all, which in turn
 * is what lets the game find its data files and write saves.
 *
 * Returns the capacity in 512-byte sectors, or 0 if the CSD could not
 * be read or is a version we do not decode.
 */
static DWORD sd_read_csd_sectors(uint8_t *csd)
{
    uint8_t version;
    uint32_t c_size;
    uint32_t capacity_bytes;

    /* CMD9 returns R2: the card sends the 16-byte CSD straight after
     * the usual R1 byte, so keep CS asserted and clock it in. */
    cs_low();

    sd_write_byte(CMD_SEND_CSD);
    sd_write_byte(0);
    sd_write_byte(0);
    sd_write_byte(0);
    sd_write_byte(0);
    sd_write_byte(0xFF);

    /* R1 */
    (void)sd_read_byte();

    /* 16 bytes of CSD */
    for (int i = 0; i < 16; i++)
        csd[i] = sd_read_byte();

    /* Let the card release the bus. */
    cs_high();
    sd_wait_ready(50);

    version = (uint8_t)(csd[0] >> 6);

    if (version == 0) {
        /* CSD v1.0 (SDSC): up to 2GB.
         *   READ_BL_LEN  = csd[5] bits 7-4  (always 9 for 512)
         *   C_SIZE       = csd[6] bits 3-0 concatenated with csd[7]
         *   C_SIZE_MULT  = csd[8] bits 2-0 */
        uint32_t read_bl_len = (csd[5] >> 4) & 0x0F;
        uint32_t c_size_mult = csd[8] & 0x07; /* C_SIZE_MULT is 3 bits (csd[8] bits 2-0) */

        c_size = ((uint32_t)(csd[6] & 0x0F) << 8) | csd[7];

        capacity_bytes = (c_size + 1) << (c_size_mult + read_bl_len);
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

static bool sd_write_block(const uint8_t *buf)
{
    cs_low();

    sd_write_byte(TOKEN_START_BLOCK);
    for (int i = 0; i < 512; i++)
        sd_write_byte(buf[i]);

    /*
     * Data CRC. For non-DMA SPI the block is sent byte-by-byte, so the
     * card cannot compute it; sending 0x00 disables the check, which is
     * what the standard recommends for byte-at-a-time transfers.
     */
    sd_write_byte(0x00);
    sd_write_byte(0x00);

    cs_high();

    /* Wait for the write to complete. */
    if (!sd_wait_ready(300))
        return false;

    return true;
}

/* --- init ----------------------------------------------------------- */

bool sd_init(void)
{
    uint8_t r;
    uint8_t ocr[4];

    gpio_init(SD_PIN_SCK);
    gpio_init(SD_PIN_MOSI);
    gpio_init(SD_PIN_MISO);
    gpio_init(SD_PIN_CS);

    gpio_set_function(SD_PIN_SCK, GPIO_FUNC_SPI);
    gpio_set_function(SD_PIN_MOSI, GPIO_FUNC_SPI);
    gpio_set_function(SD_PIN_MISO, GPIO_FUNC_SPI);
    gpio_set_function(SD_PIN_CS, GPIO_FUNC_SIO);
    gpio_set_dir(SD_PIN_CS, true);

    spi_init(SD_SPI, SD_SPEED_INIT);
    spi_set_format(SD_SPI, 8, 0, 0, SPI_MSB_FIRST);

    /* 80 clocks with CS high puts the card into native SPI mode. */
    cs_high();
    for (int i = 0; i < 80; i++)
        sd_write_byte(0xFF);
    cs_low();

    /* CMD0 leaves the card in SPI mode and idle state. */
    r = sd_command(CMD_GO_IDLE, CMD0_ARGS);
    (void)r;
    if (!sd_wait_ready(500))
        return false;

    /* CMD8: verify voltage range. Older cards ignore it. */
    r = sd_command(CMD_SEND_IF_COND, CMD8_ARGS);
    sd_is_spi_mode = (r == 0x01);

    /* ACMD41 repeatedly until the card leaves idle state. */
    {
        bool done = false;
        for (int i = 0; i < 200 && !done; i++) {
            sd_command(CMD_SEND_OP_COND, 0x40000000);
            r = sd_command(CMD_APP_OP_COND, 0x40000000);
            if (r == 0)
                done = true;
            sleep_ms(1);
        }
        if (!done)
            return false;
    }

    /* Read OCR to learn the addressing mode. */
    cs_low();
    sd_write_byte(CMD_APP_OP_COND | 58); /* CMD58 */
    sd_write_byte(0);
    sd_write_byte(0);
    sd_write_byte(0);
    sd_write_byte(0);
    sd_write_byte(0xFF);
    /* Skip one byte of R3. */
    (void)sd_read_byte();
    for (int i = 0; i < 4; i++)
        ocr[i] = sd_read_byte();
    cs_high();
    (void)ocr;

    /* 512-byte block length. */
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
        if (!sd_read_block(buff))
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
        if (!sd_write_block(buff))
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