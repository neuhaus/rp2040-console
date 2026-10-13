/*
 * Host tool: verify the data-library archive with the same inflater the board
 * runs.
 *
 * port/nhdlb.c decodes "nhdata" with uzlib; this links that same uzlib
 * (uzlib/tinflate.c) and walks the archive the way the target does, comparing
 * every decoded entry against the file it was built from.  A bad offset,
 * length or stream is then caught at build time rather than as a panic in the
 * middle of startup.
 *
 * Usage:
 *   nhdata_check <archive> [<dat-dir>]
 *
 * With <dat-dir>, each entry is compared byte-for-byte with <dat-dir>/<name>;
 * without it, the archive is only checked for decodable, correctly-sized
 * entries.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "uzlib.h"

#define NH_NAME_MAX 16
#define NH_WINDOW_MIN 10
#define NH_WINDOW_MAX 15

struct nh_header {
    char magic[4];
    unsigned int version;
    unsigned int count;
};

struct nh_dir_ent {
    char name[NH_NAME_MAX];
    unsigned int off;
    unsigned int clen;
    unsigned int ulen;
    unsigned int wbits;
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
        perror(path);
        free(buf);
        fclose(fp);
        return NULL;
    }
    fclose(fp);
    *size = (size_t) n;
    return buf;
}

int
main(int argc, char **argv)
{
    unsigned char *archive, *dict;
    size_t alen;
    struct nh_header hdr;
    unsigned int i;
    int failures = 0;
    const char *datdir = (argc > 2) ? argv[2] : NULL;

    if (argc < 2) {
        fprintf(stderr, "usage: %s <archive> [<dat-dir>]\n", argv[0]);
        return 2;
    }
    archive = slurp(argv[1], &alen);
    if (!archive)
        return 2;
    if (alen < sizeof hdr || memcmp(archive, "NHDA", 4) != 0) {
        fprintf(stderr, "nhdata_check: not an NHDA archive\n");
        return 2;
    }
    memcpy(&hdr, archive, sizeof hdr);
    if (hdr.version != 1) {
        fprintf(stderr, "nhdata_check: bad version %u\n", hdr.version);
        return 2;
    }

    dict = malloc(1U << NH_WINDOW_MAX);
    if (!dict)
        return 2;

    for (i = 0; i < hdr.count; i++) {
        struct nh_dir_ent ent;
        unsigned char *out;
        uzlib_uncomp_t d;
        int res;

        memcpy(&ent, archive + sizeof hdr + i * sizeof ent, sizeof ent);
        ent.name[NH_NAME_MAX - 1] = '\0';
        if (ent.wbits < NH_WINDOW_MIN || ent.wbits > NH_WINDOW_MAX) {
            fprintf(stderr, "nhdata_check: %s: bad window %u\n", ent.name,
                    ent.wbits);
            failures++;
            continue;
        }
        if ((size_t) ent.off + ent.clen > alen) {
            fprintf(stderr, "nhdata_check: %s: stream past end of archive\n",
                    ent.name);
            failures++;
            continue;
        }
        out = malloc(ent.ulen ? ent.ulen : 1);
        uzlib_uncompress_init(&d, dict, 1U << ent.wbits);
        d.source = archive + ent.off;
        d.source_limit = archive + ent.off + ent.clen;
        d.dest = out;
        d.dest_limit = out + ent.ulen;
        res = uzlib_uncompress(&d);
        if ((res != UZLIB_OK && res != UZLIB_DONE)
            || d.dest != out + ent.ulen) {
            fprintf(stderr, "nhdata_check: %s: inflate failed (%d, %ld/%u)\n",
                    ent.name, res, (long) (d.dest - out), ent.ulen);
            failures++;
            free(out);
            continue;
        }
        if (datdir) {
            char path[1024];
            unsigned char *orig;
            size_t olen;

            snprintf(path, sizeof path, "%s/%s", datdir, ent.name);
            orig = slurp(path, &olen);
            if (!orig || olen != ent.ulen || memcmp(orig, out, olen) != 0) {
                fprintf(stderr, "nhdata_check: %s: mismatch against %s\n",
                        ent.name, path);
                failures++;
            }
            free(orig);
        }
        free(out);
    }
    free(dict);
    free(archive);

    if (failures) {
        fprintf(stderr, "nhdata_check: %d failures\n", failures);
        return 1;
    }
    fprintf(stderr, "nhdata_check: %u entries verified\n", hdr.count);
    return 0;
}
