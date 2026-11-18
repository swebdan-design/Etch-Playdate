/*
 * ETCH CRANK -- by Synth Centric -- for Playdate
 * stdio for ETCH DS's storage.c (and the sidecar), on the Playdate's file API.
 *
 * engine/storage.c is ETCH DS's save code, unmodified: portable stdio with
 * ETCH's write order (invalidate, data, checksum what is on the card, header,
 * magic last, read back), each phase closing the file.  The Playdate has no
 * POSIX files and its file API has no read-and-update mode, so the build
 * links this layer under storage.c's calls (-Wl,--wrap=fopen,...):
 *
 *   fopen   reads the whole file into RAM (save files are 256 KB)
 *   fread / fwrite / fseek / ftell   work on that copy
 *   fclose  if anything was written: writes the copy to NAME.tmp, then
 *           renames it over NAME
 *
 * So every phase of ETCH's order reaches flash as one atomic replacement of
 * the file: an interrupted save leaves either the previous phase or the next,
 * never a torn slot, and the checksum-first, magic-last order still decides
 * what counts as valid.  Files live in the game's data folder.
 *
 * Main loop only (never the audio callback).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pd_api.h"

static PlaydateAPI *pd;
void pdfile_init(PlaydateAPI *api) { pd = api; }

typedef struct {
    char     name[64];
    uint8_t *buf;
    long     len, cap, pos;
    uint8_t  dirty, writable;
} vfile_t;

#define MAX_OPEN 2
static vfile_t files[MAX_OPEN];
static uint8_t used[MAX_OPEN];

FILE *__wrap_fopen(const char *path, const char *mode)
{
    int i, want_write = strchr(mode, 'w') || strchr(mode, '+') || strchr(mode, 'a');
    int create = strchr(mode, 'w') != NULL;
    vfile_t *v = NULL;
    SDFile *f;
    if (!pd) return NULL;
    for (i = 0; i < MAX_OPEN; i++) if (!used[i]) { v = &files[i]; used[i] = 1; break; }
    if (!v) return NULL;
    memset(v, 0, sizeof *v);
    snprintf(v->name, sizeof v->name, "%s", path[0] == '/' ? strrchr(path, '/') + 1 : path);
    v->writable = (uint8_t)want_write;
    {   /* a crash between unlink and rename leaves only NAME.tmp: recover it */
        char tmp[72];
        FileStat st;
        snprintf(tmp, sizeof tmp, "%s.tmp", v->name);
        if (pd->file->stat(v->name, &st) != 0 && pd->file->stat(tmp, &st) == 0)
            pd->file->rename(tmp, v->name);
    }
    f = create ? NULL : pd->file->open(v->name, kFileReadData);
    if (!f && !create) { used[i] = 0; return NULL; }          /* "r", "r+": must exist */
    if (f) {
        int n;
        pd->file->seek(f, 0, SEEK_END);
        v->len = pd->file->tell(f);
        pd->file->seek(f, 0, SEEK_SET);
        v->cap = v->len > 0 ? v->len : 1;
        v->buf = (uint8_t *)malloc((size_t)v->cap);
        if (!v->buf) { pd->file->close(f); used[i] = 0; return NULL; }
        n = v->len ? pd->file->read(f, v->buf, (unsigned)v->len) : 0;
        pd->file->close(f);
        if (n != v->len) { free(v->buf); used[i] = 0; return NULL; }
    } else {
        v->cap = 4096;
        v->buf = (uint8_t *)malloc((size_t)v->cap);
        if (!v->buf) { used[i] = 0; return NULL; }
        v->dirty = 1;                          /* "w": creates the file */
    }
    return (FILE *)v;
}

static vfile_t *vf(FILE *f)
{
    int i;
    for (i = 0; i < MAX_OPEN; i++) if (used[i] && (FILE *)&files[i] == f) return &files[i];
    return NULL;
}

size_t __wrap_fread(void *p, size_t sz, size_t n, FILE *f)
{
    vfile_t *v = vf(f);
    long want = (long)(sz * n), have;
    if (!v || !sz) return 0;
    have = v->len - v->pos;
    if (have < 0) have = 0;
    if (want > have) want = have - have % (long)sz;
    memcpy(p, v->buf + v->pos, (size_t)want);
    v->pos += want;
    return (size_t)want / sz;
}

size_t __wrap_fwrite(const void *p, size_t sz, size_t n, FILE *f)
{
    vfile_t *v = vf(f);
    long want = (long)(sz * n), end;
    if (!v || !v->writable || !sz) return 0;
    end = v->pos + want;
    if (end > v->cap) {
        long nc = v->cap;
        uint8_t *nb;
        while (nc < end) nc *= 2;
        nb = (uint8_t *)realloc(v->buf, (size_t)nc);
        if (!nb) return 0;
        v->buf = nb;
        v->cap = nc;
    }
    if (v->pos > v->len) memset(v->buf + v->len, 0, (size_t)(v->pos - v->len));
    memcpy(v->buf + v->pos, p, (size_t)want);
    v->pos = end;
    if (end > v->len) v->len = end;
    v->dirty = 1;
    return n;
}

int __wrap_fseek(FILE *f, long off, int whence)
{
    vfile_t *v = vf(f);
    long np;
    if (!v) return -1;
    np = whence == SEEK_SET ? off : whence == SEEK_CUR ? v->pos + off : v->len + off;
    if (np < 0) return -1;
    v->pos = np;
    return 0;
}

long __wrap_ftell(FILE *f)
{
    vfile_t *v = vf(f);
    return v ? v->pos : -1;
}

int __wrap_fflush(FILE *f) { (void)f; return 0; }

int __wrap_fclose(FILE *f)
{
    vfile_t *v = vf(f);
    int ok = 1, i = (int)(v - files);
    if (!v) return -1;
    if (v->dirty) {
        char tmp[72];
        SDFile *o;
        snprintf(tmp, sizeof tmp, "%s.tmp", v->name);
        o = pd->file->open(tmp, kFileWrite);
        ok = o && pd->file->write(o, v->buf, (unsigned)v->len) == v->len;
        if (o) ok = (pd->file->close(o) == 0) && ok;
        if (ok) {
            pd->file->unlink(v->name, 0);
            ok = pd->file->rename(tmp, v->name) == 0;
        }
    }
    free(v->buf);
    used[i] = 0;
    return ok ? 0 : -1;
}
