/* NetHack 3.6	dlb.c	$NHDT-Date: 1446975464 2015/11/08 09:37:44 $ */
/* Copyright (c) Kenneth Lorber, Bethesda, Maryland, 1993. */
/* NetHack may be freely redistributed.  See license for details. */
/*
 * rp2040-console replacement for the data librarian.
 *
 * NetHack reads every read-only data file - the dungeon and level
 * descriptions, quest texts, the encyclopedia, rumors, per-monster text,
 * help screens, the symbol set - through the dlb_* interface.  With DLB
 * defined, those calls land here instead of on stdio.
 *
 * All of those files live in one raw-deflate archive, "nhdata":
 *
 *     "NHDA" | u32 version | u32 count
 *     count * { char name[16]; u32 off; u32 clen; u32 ulen; }
 *     <count concatenated raw deflate streams>
 *
 * One archive rather than a hundred separate files, because a 2 KB FAT
 * cluster turns each small level file into a 2 KB allocation; and
 * compressed rather than not, because the program plus the data does not
 * otherwise fit in 2 MB of flash.
 *
 * Raw deflate rather than Tamp (which the level and save files use): on
 * this data deflate is 15-25% smaller - it adds Huffman coding on top of
 * LZ77, which Tamp lacks - and the decoder is just as small.  The window is
 * chosen per entry by build.py and recorded in the directory: a big text file
 * read once mid-game uses a wide one, while a level template - read while a
 * new level is being built, when free memory is tightest - stays small.  The
 * encoder is run with the same window, so every back-reference stays in
 * range.
 *
 * Each open handle owns its own FILE* and inflate state, so two data files
 * can be open at once without sharing a read position.  Seeking is emulated
 * by decoding and discarding; a backward seek restarts the entry.  The files
 * are small enough that this is cheaper than the flash it saves, and
 * NetHack's access patterns are almost entirely forward scans.
 */
#include "config.h"
#include "dlb.h"

#if defined(DLB)
#include <string.h>
#include "uzlib.h"

#define NHDATA_FILE "nhdata"
#define NH_NAME_MAX 16
#define NH_INBUF 1024
/* The deflate window is chosen per entry by build.py and recorded in the
 * directory, so a big text file read once mid-game can use a wide window
 * while a level template - read while a new level is being built - stays
 * small.  Each open handle allocates 1<<wbits bytes for its dictionary. */
#define NH_WINDOW_MIN 10
#define NH_WINDOW_MAX 15

struct nh_header {
    char magic[4];
    unsigned int version;
    unsigned int count;
};

struct nh_dir_ent {
    char name[NH_NAME_MAX];
    unsigned int off;   /* absolute offset of this entry's deflate stream */
    unsigned int clen;  /* compressed length */
    unsigned int ulen;  /* uncompressed length */
    unsigned int wbits; /* the reader allocates a 1<<wbits dictionary */
};

struct nh_stream {
    uzlib_uncomp_t dec;
    unsigned char *window; /* inflate dictionary (back-reference ring) */
    unsigned int window_bits;
    FILE *fp;
    unsigned long ulen;    /* uncompressed length */
    unsigned long upos;    /* uncompressed read position */
    unsigned long coff;    /* start of the entry in the archive */
    unsigned long cend;    /* first byte past the entry */
    unsigned long cpos;    /* compressed bytes read so far this entry */
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

/* uzlib pulls input through this when its window empties.  The callback
 * refills from the archive and returns the first byte; the rest of the
 * buffer is handed to uzlib via source/source_limit.  It stops at the end
 * of this entry, never reading into the next stream. */
static int
nh_source_read(void *arg)
{
    struct nh_stream *st = (struct nh_stream *) arg;
    size_t want, n;

    if (st->cpos >= st->cend)
        return -1;
    want = (size_t) (st->cend - st->cpos);
    if (want > sizeof st->inbuf)
        want = sizeof st->inbuf;
    n = fread(st->inbuf, 1, want, st->fp);
    if (n == 0)
        return -1;
    st->cpos += n;
    st->dec.source_limit = st->inbuf + n;
    st->dec.source = st->inbuf + 1; /* the returned byte was inbuf[0] */
    return st->inbuf[0];
}

/* Begin a fresh inflate over the entry's stream.  fp must already be
 * positioned at ent->off. */
static struct nh_stream *
nh_open(FILE *fp, const struct nh_dir_ent *ent)
{
    struct nh_stream *st;

    if (ent->wbits < NH_WINDOW_MIN || ent->wbits > NH_WINDOW_MAX)
        return (struct nh_stream *) 0; /* corrupt directory */
    st = (struct nh_stream *) alloc(sizeof *st);
    memset(st, 0, sizeof *st);
    st->fp = fp;
    st->ulen = ent->ulen;
    st->coff = ent->off;
    st->cend = (unsigned long) ent->off + ent->clen;
    st->cpos = ent->off;
    st->window_bits = ent->wbits;
    st->window = (unsigned char *) alloc(1U << ent->wbits);
    uzlib_uncompress_init(&st->dec, st->window, 1U << ent->wbits);
    st->dec.source_read_data = st;
    st->dec.source_read_cb = nh_source_read;
    st->dec.source = st->dec.source_limit = st->inbuf; /* force a refill */
    return st;
}

/* Restart the entry from its beginning, for a backward seek. */
static boolean
nh_rewind(struct nh_stream *st, FILE *fp)
{
    if (fseek(fp, (long) st->coff, SEEK_SET) != 0)
        return FALSE;
    st->cpos = st->coff;
    st->upos = 0;
    uzlib_uncompress_init(&st->dec, st->window, 1U << st->window_bits);
    st->dec.source_read_data = st;
    st->dec.source_read_cb = nh_source_read;
    st->dec.source = st->dec.source_limit = st->inbuf;
    return TRUE;
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
    if (fseek(fp, (long) ent->off, SEEK_SET) != 0) {
        (void) fclose(fp);
        return (dlb *) 0;
    }
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
    unsigned char *out = (unsigned char *) buf;

    if (size <= 0 || quan <= 0)
        return 0;
    total = (unsigned long) size * (unsigned long) quan;
    if (total > st->ulen - st->upos)
        total = st->ulen - st->upos;

    while (got < total) {
        int res;

        st->dec.dest = out + got;
        st->dec.dest_limit = out + total;
        res = uzlib_uncompress(&st->dec);
        got = (unsigned long) (st->dec.dest - out);
        if (res == UZLIB_DONE || res < 0)
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
 * fgets().  funopen's read hook streams from the inflater and its close hook
 * releases the dlb handle, so the file is never copied whole into memory;
 * only the decoder window is held, as for any other entry.
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
