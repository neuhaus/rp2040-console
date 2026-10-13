/* NetHack 3.6	dlb.c	$NHDT-Date: 1446975464 2015/11/08 09:37:44 $ */
/* Copyright (c) Kenneth Lorber, Bethesda, Maryland, 1993. */
/* NetHack may be freely redistributed.  See license for details. */
/*
 * rp2040-console replacement for the data librarian.
 *
 * NetHack reads every read-only data file - the dungeon and level
 * descriptions, quest texts, the encyclopedia, rumors, per-monster text,
 * help screens - through the dlb_* interface.  With DLB defined, those
 * calls land here instead of on stdio.
 *
 * All of those files live in one Tamp-compressed archive, "nhdata":
 *
 *     "NHDA" | u32 version | u32 count
 *     count * { char name[12]; u32 off; u32 clen; u32 ulen; }
 *     <count concatenated Tamp streams>
 *
 * One archive rather than a hundred separate files, because a 2 KB FAT
 * cluster turns each small level file into a 2 KB allocation; and
 * compressed rather than not, because the program plus the data does not
 * otherwise fit in 2 MB of flash.  Tamp's decoder needs only its window
 * (1-4 KB per open file) and its decompressor is about 2 KB of code.
 *
 * Each open handle owns its own FILE* and decompressor, so two data files
 * can be open at once without sharing a read position.  Seeking is emulated
 * by decoding and discarding; a backward seek restarts the entry from its
 * first byte.  The files are small enough that this is cheaper than the
 * flash it saves, and NetHack's access patterns are almost entirely forward
 * scans.
 */
#include "config.h"
#include "dlb.h"

#if defined(DLB)
#include <string.h>
#include "decompressor.h"

#define NHDATA_FILE "nhdata"
#define NH_NAME_MAX 16
#define NH_INBUF 512

struct nh_header {
    char magic[4];
    unsigned int version;
    unsigned int count;
};

struct nh_dir_ent {
    char name[NH_NAME_MAX];
    unsigned int off;  /* absolute offset of this entry's Tamp stream */
    unsigned int clen; /* compressed length */
    unsigned int ulen; /* uncompressed length */
};

struct nh_stream {
    TampDecompressor dec;
    TampConf conf;
    unsigned char *window;
    unsigned long ulen; /* uncompressed length */
    unsigned long upos; /* uncompressed read position */
    unsigned long coff; /* start of the entry in the archive */
    unsigned long hoff; /* bytes of Tamp header consumed at that start */
    unsigned long cpos; /* compressed bytes read so far this entry */
    unsigned long cend; /* compressed bytes in this entry */
    size_t in_pos, in_len;
    unsigned char inbuf[NH_INBUF];
};

/* The directory is read once and kept for the session. */
static struct nh_dir_ent *nh_dir;
static unsigned int nh_count;

static void
nh_unload(void)
{
    if (nh_dir) {
        free((genericptr_t) nh_dir);
        nh_dir = 0;
    }
    nh_count = 0;
}

/* Read the archive directory; return TRUE if it is available.  Failure is
 * not cached: dlb_init() can run before the working directory is set. */
static boolean
nh_load(void)
{
    FILE *fp;
    struct nh_header hdr;
    size_t n;

    if (nh_dir)
        return TRUE;

    fp = fopen(NHDATA_FILE, "rb");
    if (!fp)
        return FALSE;
    if (fread(&hdr, sizeof hdr, 1, fp) != 1
        || memcmp(hdr.magic, "NHDA", 4) != 0 || hdr.version != 1
        || hdr.count == 0 || hdr.count > 4096) {
        (void) fclose(fp);
        return FALSE;
    }
    nh_dir = (struct nh_dir_ent *) alloc(hdr.count * sizeof *nh_dir);
    n = fread(nh_dir, sizeof *nh_dir, hdr.count, fp);
    (void) fclose(fp);
    if (n != hdr.count) {
        nh_unload();
        return FALSE;
    }
    nh_count = hdr.count;
    return TRUE;
}

static struct nh_dir_ent *
nh_find(const char *name)
{
    unsigned int i;

    for (i = 0; i < nh_count; i++)
        if (!strcmp(nh_dir[i].name, name))
            return &nh_dir[i];
    return (struct nh_dir_ent *) 0;
}

static void
nh_close(struct nh_stream *st)
{
    if (!st)
        return;
    if (st->window)
        free((genericptr_t) st->window);
    free((genericptr_t) st);
}

/* Position a fresh decompressor over the entry's stream.  fp must already
 * be open; on success its position is left after the entry's Tamp header. */
static struct nh_stream *
nh_open(FILE *fp, const struct nh_dir_ent *ent)
{
    struct nh_stream *st;
    size_t want, consumed = 0;
    tamp_res res;

    if (fseek(fp, (long) ent->off, SEEK_SET) != 0)
        return (struct nh_stream *) 0;

    st = (struct nh_stream *) alloc(sizeof *st);
    memset(st, 0, sizeof *st);
    st->ulen = ent->ulen;
    st->coff = ent->off;
    st->cend = ent->clen;

    want = ent->clen < NH_INBUF ? ent->clen : NH_INBUF;
    st->in_len = fread(st->inbuf, 1, want, fp);
    st->cpos = st->in_len;
    if (st->in_len == 0) {
        nh_close(st);
        return (struct nh_stream *) 0;
    }
    res = tamp_decompressor_read_header(&st->conf, st->inbuf, st->in_len,
                                        &consumed);
    if (res != TAMP_OK) {
        nh_close(st);
        return (struct nh_stream *) 0;
    }
    st->in_pos = consumed;
    st->hoff = consumed;
    st->window = (unsigned char *) alloc((unsigned) (1 << st->conf.window));
    if (tamp_decompressor_init(&st->dec, &st->conf, st->window,
                               (uint8_t) st->conf.window) != TAMP_OK) {
        nh_close(st);
        return (struct nh_stream *) 0;
    }
    return st;
}

/* Restart the entry from its beginning, for a backward seek. */
static boolean
nh_rewind(struct nh_stream *st, FILE *fp)
{
    size_t want;

    if (fseek(fp, (long) (st->coff + st->hoff), SEEK_SET) != 0)
        return FALSE;
    want = (st->cend - st->hoff) < NH_INBUF ? (size_t) (st->cend - st->hoff)
                                            : NH_INBUF;
    st->in_len = fread(st->inbuf, 1, want, fp);
    st->in_pos = 0;
    st->cpos = st->hoff + st->in_len;
    st->upos = 0;
    if (st->in_len == 0)
        return FALSE;
    return tamp_decompressor_init(&st->dec, &st->conf, st->window,
                                  (uint8_t) st->conf.window)
           == TAMP_OK;
}

/* Refill the compressed-input buffer from the archive.  Returns the number
 * of bytes made available, or 0 at the end of the entry. */
static size_t
nh_fill(struct nh_stream *st, FILE *fp)
{
    size_t avail, want;

    avail = (size_t) (st->cend - st->cpos);
    want = avail < NH_INBUF ? avail : NH_INBUF;
    if (want == 0)
        return 0;
    st->in_len = fread(st->inbuf, 1, want, fp);
    st->in_pos = 0;
    st->cpos += st->in_len;
    return st->in_len;
}

boolean
dlb_init(void)
{
    return nh_load();
}

void
dlb_cleanup(void)
{
    nh_unload();
}

dlb *
dlb_fopen(const char *name, const char *mode)
{
    struct nh_dir_ent *ent;
    struct nh_stream *st;
    dlb *dp;
    FILE *fp;

    if (!name || !mode || mode[0] != 'r')
        return (dlb *) 0;
    if (!nh_load())
        return (dlb *) 0;
    ent = nh_find(name);
    if (!ent)
        return (dlb *) 0;

    fp = fopen(NHDATA_FILE, "rb");
    if (!fp)
        return (dlb *) 0;
    st = nh_open(fp, ent);
    if (!st) {
        (void) fclose(fp);
        return (dlb *) 0;
    }
    dp = (dlb *) alloc(sizeof *dp);
    dp->fp = fp;
    dp->lib = (library *) st;
    dp->start = 0;
    dp->size = (long) ent->ulen;
    dp->mark = 0;
    return dp;
}

int
dlb_fclose(dlb *dp)
{
    if (!dp)
        return EOF;
    if (dp->fp)
        (void) fclose(dp->fp);
    nh_close((struct nh_stream *) dp->lib);
    free((genericptr_t) dp);
    return 0;
}

int
dlb_fread(char *buf, int size, int quan, dlb *dp)
{
    struct nh_stream *st = (struct nh_stream *) dp->lib;
    unsigned long total, got = 0;

    if (size <= 0 || quan <= 0)
        return 0;
    total = (unsigned long) size * (unsigned long) quan;
    if (total > st->ulen - st->upos)
        total = st->ulen - st->upos;

    while (got < total) {
        size_t out_written = 0, in_consumed = 0, remain;
        tamp_res res;

        /* Refill only when the current buffer is spent; a refill that finds
         * nothing is fine, because the decoder may still be draining a match
         * it already read.  The no-progress check below ends the loop. */
        if (st->in_pos >= st->in_len)
            (void) nh_fill(st, dp->fp);
        remain = (size_t) (total - got);
        res = tamp_decompressor_decompress(&st->dec, (unsigned char *) buf + got,
                                           remain, &out_written,
                                           st->inbuf + st->in_pos,
                                           st->in_len - st->in_pos, &in_consumed);
        st->in_pos += in_consumed;
        got += out_written;
        if (res < 0)
            break;
        if (out_written == 0 && in_consumed == 0)
            break;
    }
    st->upos += got;
    dp->mark = (long) st->upos;
    return (int) (got / (unsigned long) size);
}

int
dlb_fseek(dlb *dp, long pos, int whence)
{
    struct nh_stream *st = (struct nh_stream *) dp->lib;
    long target;

    switch (whence) {
    case SEEK_SET:
        target = pos;
        break;
    case SEEK_CUR:
        target = (long) st->upos + pos;
        break;
    case SEEK_END:
        target = (long) st->ulen + pos;
        break;
    default:
        return -1;
    }
    if (target < 0)
        return -1;
    if ((unsigned long) target > st->ulen)
        target = (long) st->ulen;

    if ((unsigned long) target < st->upos) {
        if (!nh_rewind(st, dp->fp))
            return -1;
    }
    while ((unsigned long) target > st->upos) {
        char scratch[256];
        unsigned long n = (unsigned long) target - st->upos;

        if (n > sizeof scratch)
            n = sizeof scratch;
        if (dlb_fread(scratch, 1, (int) n, dp) != (int) n)
            return -1;
    }
    dp->mark = (long) st->upos;
    return 0;
}

char *
dlb_fgets(char *buf, int len, dlb *dp)
{
    int i = 0;

    if (len <= 0)
        return (char *) 0;
    while (i < len - 1) {
        int c = dlb_fgetc(dp);

        if (c == EOF) {
            if (i == 0)
                return (char *) 0;
            break;
        }
        buf[i++] = (char) c;
        if (c == '\n')
            break;
    }
    buf[i] = '\0';
    return buf;
}

int
dlb_fgetc(dlb *dp)
{
    char c;

    if (dlb_fread(&c, 1, 1, dp) != 1)
        return EOF;
    return (unsigned char) c;
}

long
dlb_ftell(dlb *dp)
{
    return (long) ((struct nh_stream *) dp->lib)->upos;
}

/*
 * Serve a DLB entry as a stdio FILE*, for the one reader that wants stdio
 * rather than the dlb_* calls - the symbol-set parser reads "symbols" with
 * fgets().  funopen's read hook streams from the Tamp decoder and its close
 * hook releases the dlb handle, so the file is never copied whole into
 * memory; only the decoder window is held, as for any other entry.
 */
static int
nh_memread(void *cookie, char *buf, int len)
{
    return (len > 0) ? dlb_fread(buf, 1, len, (dlb *) cookie) : 0;
}

static int
nh_memclose(void *cookie)
{
    (void) dlb_fclose((dlb *) cookie);
    return 0;
}

FILE *
nhdlb_memopen(const char *name)
{
    dlb *dp = dlb_fopen(name, "r");
    FILE *fp;

    if (!dp)
        return (FILE *) 0;
    fp = funopen(dp, nh_memread, (int (*)(void *, const char *, int)) 0,
                 (fpos_t (*)(void *, fpos_t, int)) 0, nh_memclose);
    if (!fp) {
        (void) dlb_fclose(dp);
        return (FILE *) 0;
    }
    return fp;
}

#endif /* DLB */
