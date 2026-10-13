/*
 * Host tool: verify a Tamp data archive round-trips.
 *
 * The board has no way to report why a data file failed to decompress, and
 * the failure shows up as a panic in the middle of startup.  This walks the
 * archive the same way the target's dlb layer does and compares every entry
 * against the file it was built from, so a bad offset, length or stream is
 * caught where it can still be printed.
 *
 * Usage:
 *   tamp_check <archive> <name> <original> [<name> <original>]...
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "decompressor.h"

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

static unsigned char *
slurp(const char *path, size_t *size)
{
    unsigned char *buf;
    long n;
    FILE *fp = fopen(path, "rb");

    if (!fp) {
        perror(path);
        return NULL;
    }
    if (fseek(fp, 0, SEEK_END) != 0 || (n = ftell(fp)) < 0
        || fseek(fp, 0, SEEK_SET) != 0) {
        perror(path);
        fclose(fp);
        return NULL;
    }
    buf = malloc((size_t) n ? (size_t) n : 1);
    if (!buf || fread(buf, 1, (size_t) n, fp) != (size_t) n) {
        fprintf(stderr, "%s: read failed\n", path);
        free(buf);
        fclose(fp);
        return NULL;
    }
    fclose(fp);
    *size = (size_t) n;
    return buf;
}

static int
decode(const unsigned char *comp, size_t clen, unsigned char *out,
       size_t ulen)
{
    TampDecompressor dec;
    TampConf conf;
    unsigned char *window;
    size_t consumed = 0, in_pos, out_pos = 0;
    tamp_res res;

    res = tamp_decompressor_read_header(&conf, comp,
                                        clen < 512 ? clen : 512, &consumed);
    if (res != TAMP_OK) {
        fprintf(stderr, "  header: %d\n", res);
        return -1;
    }
    window = malloc((size_t) 1 << conf.window);
    if (!window)
        return -1;
    if (tamp_decompressor_init(&dec, &conf, window, (uint8_t) conf.window)
        != TAMP_OK) {
        fprintf(stderr, "  init failed\n");
        free(window);
        return -1;
    }
    in_pos = consumed;
    while (out_pos < ulen) {
        size_t out_written = 0, in_consumed = 0, give = clen - in_pos;

        /* One output byte per call, input in 512-byte bites: exactly what
         * the target does for dlb_fgets()/dlb_fgetc(), including the
         * decoder's output-limited resumption path. */
        if (give > 512)
            give = 512;
        res = tamp_decompressor_decompress(&dec, out + out_pos, 1,
                                           &out_written, comp + in_pos, give,
                                           &in_consumed);
        in_pos += in_consumed;
        out_pos += out_written;
        if (res < 0) {
            fprintf(stderr, "  decode: %d\n", res);
            free(window);
            return -1;
        }
        if (out_written == 0 && in_consumed == 0) {
            fprintf(stderr, "  decode stalled at %zu/%zu\n", out_pos, ulen);
            free(window);
            return -1;
        }
    }
    free(window);
    return 0;
}

int
main(int argc, char **argv)
{
    unsigned char *archive;
    size_t archive_len;
    struct nh_header hdr;
    struct nh_dir_ent *dir;
    unsigned int i, count;
    int failures = 0;

    if (argc < 4 || (argc - 2) % 2 != 0) {
        fprintf(stderr, "usage: %s <archive> <name> <original> [...]\n",
                argv[0]);
        return 2;
    }
    archive = slurp(argv[1], &archive_len);
    if (!archive)
        return 1;
    if (archive_len < sizeof hdr) {
        fprintf(stderr, "archive too short\n");
        return 1;
    }
    memcpy(&hdr, archive, sizeof hdr);
    if (memcmp(hdr.magic, "NHDA", 4) != 0 || hdr.version != 1) {
        fprintf(stderr, "bad archive header\n");
        return 1;
    }
    count = hdr.count;
    dir = (struct nh_dir_ent *) (archive + sizeof hdr);
    if (sizeof hdr + (size_t) count * sizeof *dir > archive_len) {
        fprintf(stderr, "directory runs past the archive\n");
        return 1;
    }

    for (i = 2; i + 1 < (unsigned int) argc; i += 2) {
        const char *name = argv[i];
        const char *orig = argv[i + 1];
        unsigned char *want, *got;
        size_t want_len;
        unsigned int j;
        struct nh_dir_ent *ent = NULL;

        for (j = 0; j < count; j++)
            if (strcmp(dir[j].name, name) == 0)
                ent = &dir[j];
        if (!ent) {
            fprintf(stderr, "FAIL %s: not in archive\n", name);
            failures++;
            continue;
        }
        if ((size_t) ent->off + ent->clen > archive_len) {
            fprintf(stderr, "FAIL %s: stream runs past the archive\n", name);
            failures++;
            continue;
        }
        want = slurp(orig, &want_len);
        if (!want) {
            failures++;
            continue;
        }
        if (want_len != ent->ulen) {
            fprintf(stderr, "FAIL %s: length %zu != directory %u\n", name,
                    want_len, ent->ulen);
            free(want);
            failures++;
            continue;
        }
        got = malloc(want_len ? want_len : 1);
        if (!got || decode(archive + ent->off, ent->clen, got, want_len) != 0
            || memcmp(got, want, want_len) != 0) {
            fprintf(stderr, "FAIL %s: round-trip mismatch\n", name);
            failures++;
        } else {
            fprintf(stderr, "  ok %-12s %7zu -> %7u\n", name, want_len,
                    ent->clen);
        }
        free(got);
        free(want);
    }

    if (failures) {
        fprintf(stderr, "tamp_check: %d failures\n", failures);
        return 1;
    }
    fprintf(stderr, "tamp_check: archive verified\n");
    return 0;
}
