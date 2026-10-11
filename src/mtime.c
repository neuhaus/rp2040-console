/*
 * Remembered modification times; see mtime.h.
 *
 * The file is fixed-size records, rewritten whole on every change. There are
 * at most a few dozen entries - saves, locks, bones - so the whole thing is
 * one sector, and a torn write fails the magic check and starts over empty,
 * which fails safe to directory times.
 */

#include <stdint.h>
#include <string.h>

#include "ff.h"

#include "mtime.h"
#include "storage.h"
#include "trace.h"

#define mtime_file   ".mtimes"
#define mtime_magic  0x324D544DU   /* "MTM2": int64 dates (MTM1 was int32) */
#define mtime_max    32
#define mtime_path   64
#define mtime_reclen (mtime_path + 8)

struct mtime_rec {
    char path[mtime_path];
    int64_t mtime;
};

static struct {
    int loaded;
    uint32_t count;
    struct mtime_rec rec[mtime_max];
} db;

static void load(void)
{
    FIL f;
    UINT br;
    uint8_t hdr[8];

    if (db.loaded || !storage_is_available())
        return;
    db.loaded = 1;

    if (f_open(&f, mtime_file, FA_READ) != FR_OK) {
        TRACE("mtime: no table on volume\n");
        return;
    }
    if (f_read(&f, hdr, sizeof(hdr), &br) != FR_OK || br != sizeof(hdr)) {
        f_close(&f);
        return;
    }
    uint32_t magic = (uint32_t)hdr[0] | ((uint32_t)hdr[1] << 8) |
                     ((uint32_t)hdr[2] << 16) | ((uint32_t)hdr[3] << 24);
    uint32_t count = (uint32_t)hdr[4] | ((uint32_t)hdr[5] << 8) |
                     ((uint32_t)hdr[6] << 16) | ((uint32_t)hdr[7] << 24);
    if (magic != mtime_magic || count > mtime_max) {
        TRACE("mtime: table failed check, starting empty\n");
        f_close(&f);
        return;
    }
    for (uint32_t i = 0; i < count; i++) {
        uint8_t raw[mtime_reclen];
        uint64_t v;
        int k;

        if (f_read(&f, raw, sizeof(raw), &br) != FR_OK ||
            br != sizeof(raw))
            break;
        memcpy(db.rec[db.count].path, raw, mtime_path);
        db.rec[db.count].path[mtime_path - 1] = '\0';
        v = 0;
        for (k = 0; k < 8; k++)
            v |= (uint64_t)raw[mtime_path + k] << (8 * k);
        db.rec[db.count].mtime = (int64_t)v;
        db.count++;
        TRACE("mtime: file has \"%s\" as %lld\n", db.rec[db.count - 1].path,
              (long long)db.rec[db.count - 1].mtime);
    }
    f_close(&f);
    TRACE("mtime: loaded %u remembered dates\n", (unsigned)db.count);
}

static void persist(void)
{
    FIL f;
    UINT bw;
    uint8_t hdr[8];

    if (!storage_is_available())
        return;

    hdr[0] = (uint8_t)mtime_magic;
    hdr[1] = (uint8_t)(mtime_magic >> 8);
    hdr[2] = (uint8_t)(mtime_magic >> 16);
    hdr[3] = (uint8_t)(mtime_magic >> 24);
    hdr[4] = (uint8_t)db.count;
    hdr[5] = (uint8_t)(db.count >> 8);
    hdr[6] = (uint8_t)(db.count >> 16);
    hdr[7] = (uint8_t)(db.count >> 24);

    if (f_open(&f, mtime_file, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) {
        TRACE("mtime: persist open failed\n");
        return;
    }
    if (f_write(&f, hdr, sizeof(hdr), &bw) != FR_OK || bw != sizeof(hdr)) {
        TRACE("mtime: persist header short\n");
        f_close(&f);
        return;
    }
    for (uint32_t i = 0; i < db.count; i++) {
        uint8_t raw[mtime_reclen];
        uint64_t v = (uint64_t)db.rec[i].mtime;
        int k;

        memset(raw, 0, sizeof(raw));
        strncpy((char *)raw, db.rec[i].path, mtime_path - 1);
        for (k = 0; k < 8; k++)
            raw[mtime_path + k] = (uint8_t)(v >> (8 * k));
        if (f_write(&f, raw, sizeof(raw), &bw) != FR_OK ||
            bw != sizeof(raw)) {
            TRACE("mtime: persist short at entry %u\n", (unsigned)i);
            break;
        }
    }
    if (f_close(&f) != FR_OK)
        TRACE("mtime: persist close failed\n");
}

static int index_of(const char *path)
{
    for (uint32_t i = 0; i < db.count; i++) {
        if (strncmp(db.rec[i].path, path, mtime_path) == 0)
            return (int)i;
    }
    return -1;
}

time_t mtime_lookup(const char *path, int *found)
{
    int i;

    load();
    i = (path == NULL) ? -1 : index_of(path);
    if (found != NULL)
        *found = (i >= 0);
    if (i >= 0)
        TRACE("mtime: \"%s\" remembered as %lld\n", path, (long long)db.rec[i].mtime);
    return (i >= 0) ? (time_t)db.rec[i].mtime : (time_t)0;
}

void mtime_store(const char *path, time_t t)
{
    int i;

    if (path == NULL || strlen(path) >= mtime_path)
        return;
    load();

    i = index_of(path);
    if (i < 0) {
        /* Full: forget the oldest. A missing stamp fails safe to the
         * directory time. */
        if (db.count >= mtime_max) {
            memmove(&db.rec[0], &db.rec[1],
                    sizeof(db.rec[0]) * (mtime_max - 1));
            db.count = mtime_max - 1;
        }
        i = (int)db.count++;
        strncpy(db.rec[i].path, path, mtime_path - 1);
        db.rec[i].path[mtime_path - 1] = '\0';
    }
    db.rec[i].mtime = (int64_t)t;
    TRACE("mtime: remember \"%s\" as %lld\n", path, (long long)t);
    persist();
}

void mtime_move(const char *oldpath, const char *newpath)
{
    int i, j;

    if (oldpath == NULL || newpath == NULL ||
        strlen(newpath) >= mtime_path)
        return;
    load();

    i = index_of(oldpath);
    if (i < 0)
        return;

    /*
     * The moved entry carries the fresh date: a save is stamped on close
     * and renamed after, with no close in between. An entry already under
     * the new name is the stale one, so it goes.
     */
    j = index_of(newpath);
    if (j >= 0 && j != i) {
        memmove(&db.rec[j], &db.rec[j + 1],
                sizeof(db.rec[0]) * (db.count - (uint32_t)j - 1u));
        db.count--;
        if (j < i)
            i--;
    }
    strncpy(db.rec[i].path, newpath, mtime_path - 1);
    db.rec[i].path[mtime_path - 1] = '\0';
    persist();
}

void mtime_drop(const char *path)
{
    int i;

    if (path == NULL)
        return;
    load();

    i = index_of(path);
    if (i < 0)
        return;
    memmove(&db.rec[i], &db.rec[i + 1],
            sizeof(db.rec[0]) * (db.count - (uint32_t)i - 1u));
    db.count--;
    persist();
}
