/* Framed Tamp over NetHack's existing zero-run stream. Independent encoder and
 * decoder permit copying cached levels into a save without a dungeon in RAM.
 * Every frame has a bounded decoded size and CRC; all physical writes are checked.
 * Save/level/bones files deliberately use a new, identifiable disk format. */
#include "tamp-codec.h"
#include "compressor.h"
#include "decompressor.h"
#include <stdint.h>
#include <string.h>
#include <unistd.h>

void nh_panic(const char *, ...);
#define CHUNK 1024
#define PACKED (CHUNK + CHUNK / 8 + 32)
static const unsigned char magic[8] = {'N','H','3','1','T','P',1,0};
static struct {
    TampCompressor codec;
    unsigned char window[1024], raw[CHUNK], packed[PACKED];
    size_t len;
    int fd, active;
} enc;
static struct {
    TampDecompressor codec;
    unsigned char window[1024], raw[CHUNK], packed[PACKED];
    size_t len, pos;
    int fd, active, eof;
} dec;

static uint32_t crc32(const unsigned char *s, size_t n) {
    uint32_t crc = ~0u;
    while (n--) {
        unsigned i;
        crc ^= *s++;
        for (i = 0; i < 8; ++i) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}
static void put16(unsigned char *p, unsigned n) { p[0] = n; p[1] = n >> 8; }
static unsigned get16(const unsigned char *p) { return p[0] | ((unsigned)p[1] << 8); }
static void checked_write(int fd, const void *p, size_t n) {
    if (write(fd, p, n) != (int)n) nh_panic("short codec write (disk full or I/O error)");
}
/* Only the explicit end marker terminates a stream; physical EOF is corruption. */
static void checked_read(int fd, void *p, size_t n) {
    size_t done = 0;
    while (done < n) {
        int got = read(fd, (unsigned char *)p + done, n - done);
        if (got <= 0) nh_panic("truncated codec frame or I/O error");
        done += (unsigned)got;
    }
}
void nh_codec_begin(int fd) {
    if (enc.active) nh_panic("nested codec writer");
    enc.fd = fd; enc.len = 0; enc.active = 1;
    checked_write(fd, magic, sizeof magic);
}
void nh_codec_flush(int fd) {
    TampConf conf = {0};
    size_t consumed = 0, written = 0, tail = 0;
    unsigned char header[8];
    uint32_t crc;
    tamp_res res;
    size_t size;
    if (!enc.active || fd != enc.fd) nh_panic("wrong codec writer");
    if (!enc.len) return;
    conf.window = 10; conf.literal = 8; conf.extended = 1;
    if (tamp_compressor_init(&enc.codec, &conf, enc.window) != TAMP_OK) nh_panic("codec encoder init");
    res = tamp_compressor_compress(&enc.codec, enc.packed, sizeof enc.packed, &written, enc.raw, enc.len, &consumed);
    if (consumed != enc.len || (res != TAMP_OK && res != TAMP_INPUT_EXHAUSTED)) nh_panic("codec encoder overflow");
    res = tamp_compressor_flush(&enc.codec, enc.packed + written, sizeof enc.packed - written, &tail, 0);
    if (res != TAMP_OK) nh_panic("codec encoder flush");
    written += tail;
    size = written < enc.len ? written : enc.len;
    put16(header, (unsigned)enc.len); put16(header + 2, (unsigned)size);
    crc = crc32(enc.raw, enc.len);
    header[4] = crc; header[5] = crc >> 8; header[6] = crc >> 16; header[7] = crc >> 24;
    checked_write(fd, header, sizeof header);
    checked_write(fd, written < enc.len ? enc.packed : enc.raw, size);
    enc.len = 0;
}
void nh_codec_write(int fd, const void *buf, size_t size) {
    const unsigned char *p = buf;
    if (!enc.active || fd != enc.fd) nh_panic("wrong codec writer");
    while (size) {
        size_t n = sizeof enc.raw - enc.len;
        if (n > size) n = size;
        memcpy(enc.raw + enc.len, p, n);
        enc.len += n; p += n; size -= n;
        if (enc.len == sizeof enc.raw) nh_codec_flush(fd);
    }
}
void nh_codec_end(int fd) {
    if (!enc.active) return; /* raw .0 lock files also call bclose */
    static const unsigned char end_marker[8] = {0};
    nh_codec_flush(fd);
    checked_write(fd, end_marker, sizeof end_marker);
    enc.active = 0;
}
void nh_codec_reset(void) { dec.active = dec.eof = 0; dec.pos = dec.len = 0; }
int nh_codec_valid(int fd) {
    unsigned char header[sizeof magic];
    long pos = lseek(fd, 0, SEEK_CUR);
    int n = read(fd, header, sizeof header);
    if (pos < 0 || lseek(fd, pos, SEEK_SET) != pos) return 0;
    return n == sizeof header && !memcmp(header, magic, sizeof magic);
}
int nh_codec_read(int fd, void *buf, size_t size) {
    unsigned char *out = buf;
    if (!dec.active) {
        unsigned char header[sizeof magic];
        checked_read(fd, header, sizeof header);
        if (memcmp(header, magic, sizeof magic)) nh_panic("incompatible codec format");
        dec.fd = fd; dec.active = 1; dec.eof = 0; dec.len = dec.pos = 0;
    }
    if (fd != dec.fd) nh_panic("codec reader not reset");
    if (dec.eof) return -1;
    while (size) {
        size_t n;
        if (dec.pos == dec.len) {
            unsigned char header[8];
            unsigned raw, packed;
            uint32_t expected;
            checked_read(fd, header, sizeof header);
            if (!memcmp(header, "\0\0\0\0\0\0\0\0", sizeof header)) {
                unsigned char extra;
                if (read(fd, &extra, 1) != 0) nh_panic("trailing codec data");
                dec.eof = 1;
                return -1;
            }
            raw = get16(header); packed = get16(header + 2);
            if (!raw || raw > CHUNK || !packed || packed > raw) nh_panic("invalid codec frame size");
            checked_read(fd, dec.packed, packed);
            if (packed == raw) memcpy(dec.raw, dec.packed, raw);
            else {
                size_t written = 0, consumed = 0;
                tamp_res res;
                if (tamp_decompressor_init(&dec.codec, 0, dec.window, 10) != TAMP_OK) nh_panic("codec decoder init");
                res = tamp_decompressor_decompress(&dec.codec, dec.raw, raw, &written, dec.packed, packed, &consumed);
                if (written != raw || (res != TAMP_OK && res != TAMP_OUTPUT_FULL && res != TAMP_INPUT_EXHAUSTED)) nh_panic("invalid compressed frame");
            }
            expected = (uint32_t)header[4] | ((uint32_t)header[5] << 8) | ((uint32_t)header[6] << 16) | ((uint32_t)header[7] << 24);
            if (crc32(dec.raw, raw) != expected) nh_panic("codec checksum mismatch");
            dec.pos = 0; dec.len = raw;
        }
        n = dec.len - dec.pos;
        if (n > size) n = size;
        memcpy(out, dec.raw + dec.pos, n);
        out += n; size -= n; dec.pos += n;
    }
    return 0;
}
