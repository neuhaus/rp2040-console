/*
 * Host tool: pack the NetHack data files into a single Tamp-compressed
 * archive for the rp2040-console port.
 *
 * The board has 2 MB of flash and the game is most of one of them, so the
 * data files are compressed.  They go into one archive rather than staying
 * separate because a FAT volume with a 2 KB cluster size turns each of the
 * ~140 tiny level files into a 2 KB allocation; collected here, the whole
 * library costs one file's worth of slack.
 *
 * Archive layout (little-endian; kept in step with port/nhdlb.c):
 *
 *   "NHDA" | u32 version | u32 count
 *   count * { char name[12]; u32 off; u32 clen; u32 ulen; }
 *   <count concatenated Tamp streams, offsets absolute in the file>
 *
 * Usage:
 *   tamp_pack <output> <name> <window_bits> <input> [<name> <bits> <input>]...
 *
 * window_bits is per file: level files are small and compress better with a
 * short window, the long text files with a longer one.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "compressor.h"

#define NH_MAGIC "NHDA"
#define NH_VERSION 1u
#define NH_NAME_MAX 16

struct nh_header {
    char magic[4];
    uint32_t version;
    uint32_t count;
};

struct nh_dir_ent {
    char name[NH_NAME_MAX];
    uint32_t off;
    uint32_t clen;
    uint32_t ulen;
};

static int
pack_one(FILE *out, const char *path, int window_bits, uint32_t *clen,
         uint32_t *ulen)
{
    TampConf conf;
    TampCompressor c;
    unsigned char *window;
    FILE *in;
    long size, start;
    tamp_res res;

    in = fopen(path, "rb");
    if (!in) {
        perror(path);
        return -1;
    }
    if (fseek(in, 0, SEEK_END) != 0 || (size = ftell(in)) < 0
        || fseek(in, 0, SEEK_SET) != 0) {
        perror(path);
        fclose(in);
        return -1;
    }
    *ulen = (uint32_t) size;

    memset(&conf, 0, sizeof conf);
    conf.window = (uint16_t) window_bits;
    conf.literal = 8;
    conf.extended = 1;

    window = malloc((size_t) 1 << window_bits);
    if (!window) {
        fclose(in);
        return -1;
    }
    res = tamp_compressor_init(&c, &conf, window);
    if (res != TAMP_OK) {
        fprintf(stderr, "tamp: init failed on %s (%d)\n", path, res);
        free(window);
        fclose(in);
        return -1;
    }

    start = ftell(out);
    res = tamp_compress_stream(&c, tamp_stream_stdio_read, in,
                               tamp_stream_stdio_write, out, NULL, NULL, NULL,
                               NULL);
    free(window);
    fclose(in);
    if (res != TAMP_OK) {
        fprintf(stderr, "tamp: compression failed on %s (%d)\n", path, res);
        return -1;
    }
    *clen = (uint32_t) (ftell(out) - start);
    return 0;
}

int
main(int argc, char **argv)
{
    struct nh_header hdr;
    struct nh_dir_ent *dir;
    const char *out_path;
    FILE *out;
    uint32_t data_off;
    int count, i;

    if (argc < 5 || (argc - 2) % 3 != 0) {
        fprintf(stderr,
                "usage: %s <output> <name> <window_bits> <input> [...]\n",
                argv[0]);
        return 2;
    }
    out_path = argv[1];
    count = (argc - 2) / 3;

    dir = calloc((size_t) count, sizeof *dir);
    if (!dir)
        return 1;

    out = fopen(out_path, "wb");
    if (!out) {
        perror(out_path);
        return 1;
    }

    memcpy(hdr.magic, NH_MAGIC, 4);
    hdr.version = NH_VERSION;
    hdr.count = (uint32_t) count;

    /* The directory is written once here (empty) and again once the data
     * offsets are known.  Writing it twice keeps every entry at a fixed
     * offset, so the reader can slurp the whole table in one read. */
    fwrite(&hdr, sizeof hdr, 1, out);
    fwrite(dir, sizeof *dir, (size_t) count, out);
    data_off = (uint32_t) (sizeof hdr + (size_t) count * sizeof *dir);

    for (i = 0; i < count; i++) {
        const char *name = argv[2 + 3 * i];
        int bits = atoi(argv[3 + 3 * i]);
        const char *path = argv[4 + 3 * i];
        uint32_t clen = 0, ulen = 0;

        if (strlen(name) >= NH_NAME_MAX) {
            fprintf(stderr, "tamp_pack: name too long: %s\n", name);
            return 1;
        }
        if (ftell(out) != (long) data_off) {
            fprintf(stderr, "tamp_pack: internal offset drift\n");
            return 1;
        }
        if (pack_one(out, path, bits, &clen, &ulen) != 0)
            return 1;

        strcpy(dir[i].name, name);
        dir[i].off = data_off;
        dir[i].clen = clen;
        dir[i].ulen = ulen;
        data_off += clen;
        fprintf(stderr, "  + %-12s %7u -> %7u (window %d)\n", name, ulen,
                clen, bits);
    }

    if (fseek(out, (long) sizeof hdr, SEEK_SET) != 0
        || fwrite(dir, sizeof *dir, (size_t) count, out) != (size_t) count) {
        perror("tamp_pack: directory rewrite");
        return 1;
    }
    fclose(out);
    fprintf(stderr, "tamp_pack: %s: %d files, %u bytes\n", out_path,
            hdr.count, data_off);
    return 0;
}
