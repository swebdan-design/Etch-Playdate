/*
 * ETCH DS -- by Synth Centric
 * Project storage on the SD card (see storage.h).  Portable C over stdio:
 * on the DS it runs on libfat/DLDI, on the host it runs in the unit tests.
 */
#include <stdio.h>
#include <string.h>
#include "storage.h"
#ifdef __NDS__
#include <fat.h>
#include <sys/stat.h>
#endif

const char *storage_path = "/etch_ds/etch_ds.sav";
const char *storage_gb_path = "/etch_ds/etch_gb.sav";

#define HDR      16
#define META_OFF (DS_SLOTS * DS_SLOT_SIZE)
#define FILE_LEN (META_OFF + 16)
#include <stddef.h>
#define DATA_LEN    ((uint32_t)(sizeof(project_t) + sizeof(ds_ext_t)))
#define DATA_LEN_V1 ((uint32_t)(sizeof(project_t) + offsetof(ds_ext_t, drums)))

static uint8_t buf[DS_SLOT_SIZE];
typedef char data_fits[(DATA_LEN + HDR <= DS_SLOT_SIZE) ? 1 : -1];

/* ETCH's Fletcher-16 (src/storage/fletcher.s): both sums with end-around
 * carry, 0xFF folded to 0 */
uint16_t etch_fletcher16(const uint8_t *p, uint32_t n)
{
    uint32_t a = 0, b = 0, i;
    for (i = 0; i < n; i++) {
        a += p[i]; a = (a & 0xFF) + (a >> 8);
        b += a;    b = (b & 0xFF) + (b >> 8);
    }
    if (a == 0xFF) a = 0;
    if (b == 0xFF) b = 0;
    return (uint16_t)(a | (b << 8));
}

static uint32_t le32(const uint8_t *p) { return p[0] | (p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

static uint8_t rd(const char *path, uint32_t off, void *dst, uint32_t n)
{
    FILE *f = fopen(path, "rb");
    uint8_t ok;
    if (!f) return 0;
    ok = fseek(f, (long)off, SEEK_SET) == 0 && fread(dst, 1, n, f) == n;
    fclose(f);
    return ok;
}

static uint8_t wr(uint32_t off, const void *src, uint32_t n)
{
    FILE *f = fopen(storage_path, "r+b");
    uint8_t ok;
    if (!f) return 0;
    ok = fseek(f, (long)off, SEEK_SET) == 0 && fwrite(src, 1, n, f) == n;
    ok = (fclose(f) == 0) && ok;                  /* each phase reaches the card */
    return ok;
}

static uint8_t ensure_file(void)
{
    FILE *f = fopen(storage_path, "rb");
    uint32_t i;
    if (f) {
        fseek(f, 0, SEEK_END);
        if (ftell(f) >= (long)FILE_LEN) { fclose(f); return 1; }
        fclose(f);
    }
    f = fopen(storage_path, f ? "r+b" : "w+b");
    if (!f) return 0;
    fseek(f, 0, SEEK_END);
    memset(buf, 0, sizeof buf);
    for (i = (uint32_t)ftell(f); i < FILE_LEN; ) {
        uint32_t n = FILE_LEN - i > sizeof buf ? (uint32_t)sizeof buf : FILE_LEN - i;
        if (fwrite(buf, 1, n, f) != n) { fclose(f); return 0; }
        i += n;
    }
    return fclose(f) == 0;
}

uint8_t storage_init(void)
{
#ifdef __NDS__
    if (!fatInitDefault()) return 0;
    mkdir("/etch_ds", 0777);
#endif
    return 1;
}

/* check slot s; on SLOT_OK the slot's data is left in buf */
static uint32_t checked_size;
static uint8_t check(uint8_t s)
{
    uint8_t h[HDR];
    uint32_t size;
    if (s >= DS_SLOTS || !rd(storage_path, s * DS_SLOT_SIZE, h, HDR)) return SLOT_EMPTY;
    if (h[0] != 'E' || h[1] != 'T' || h[2] != 'D' || h[3] != 'S') return SLOT_EMPTY;
    size = le32(h + 8);
    if (!((h[4] == DS_SAVE_VERSION && size == DATA_LEN) || (h[4] == 1 && size == DATA_LEN_V1))) return SLOT_BAD;
    if (!rd(storage_path, s * DS_SLOT_SIZE + HDR, buf, size)) return SLOT_BAD;
    if (etch_fletcher16(buf, size) != (uint16_t)(h[12] | (h[13] << 8))) return SLOT_BAD;
    checked_size = size;
    return SLOT_OK;
}

uint8_t storage_slot_status(uint8_t slot, char *name10)
{
    uint8_t r = check(slot);
    if (r == SLOT_OK && name10) memcpy(name10, buf, PROJECT_NAME_LEN);
    return r;
}

uint8_t storage_save(uint8_t slot, const project_t *p, const ds_ext_t *x)
{
    uint8_t h[HDR] = { 0 }, zero = 0, magic = 'E', m[4];
    uint16_t sum;
    uint32_t base = slot * DS_SLOT_SIZE;
    if (slot >= DS_SLOTS || !ensure_file()) return 0;
    if (!wr(base, &zero, 1)) return 0;                             /* 1: invalidate */
    memcpy(buf, p, sizeof(project_t));
    memcpy(buf + sizeof(project_t), x, sizeof(ds_ext_t));
    if (!wr(base + HDR, buf, DATA_LEN)) return 0;                  /* 2: data */
    if (!rd(storage_path, base + HDR, buf, DATA_LEN)) return 0;    /* 3: checksum of */
    sum = etch_fletcher16(buf, DATA_LEN);                          /*    what is there */
    h[1] = 'T'; h[2] = 'D'; h[3] = 'S';
    h[4] = DS_SAVE_VERSION;
    h[8] = (uint8_t)DATA_LEN; h[9] = (uint8_t)(DATA_LEN >> 8);
    h[10] = (uint8_t)(DATA_LEN >> 16); h[11] = (uint8_t)(DATA_LEN >> 24);
    h[12] = (uint8_t)sum; h[13] = (uint8_t)(sum >> 8);
    if (!wr(base + 1, h + 1, HDR - 1)) return 0;                   /* 4: header */
    if (!wr(base, &magic, 1)) return 0;                            /* 5: magic last */
    if (check(slot) != SLOT_OK) return 0;                          /* 6: read back */
    if (memcmp(buf, p, sizeof(project_t)) || memcmp(buf + sizeof(project_t), x, sizeof(ds_ext_t))) return 0;
    m[0] = 'E'; m[1] = 'M'; m[2] = slot; m[3] = (uint8_t)~slot;    /* last used slot */
    wr(META_OFF, m, 4);
    return 1;
}

uint8_t storage_load(uint8_t slot, project_t *p, ds_ext_t *x)
{
    if (check(slot) != SLOT_OK) return 0;                          /* never half-load */
    memcpy(p, buf, sizeof(project_t));
    memset(x, 0, sizeof *x);
    memcpy(x, buf + sizeof(project_t), checked_size - sizeof(project_t));
    x->has_drums = checked_size == DATA_LEN;                       /* v2 */
    return 1;
}

uint8_t storage_erase(uint8_t slot)
{
    uint8_t z[4] = { 0, 0, 0, 0 }, m[4];
    if (slot >= DS_SLOTS || !ensure_file()) return 0;
    if (!wr(slot * DS_SLOT_SIZE, z, 4)) return 0;
    if (rd(storage_path, META_OFF, m, 4) && m[0] == 'E' && m[1] == 'M' && m[2] == slot) wr(META_OFF, z, 1);
    return 1;
}

uint8_t storage_last_slot(void)
{
    uint8_t m[4];
    if (!rd(storage_path, META_OFF, m, 4)) return SAVE_NONE;
    if (m[0] == 'E' && m[1] == 'M' && m[2] < DS_SLOTS && m[3] == (uint8_t)~m[2]) return m[2];
    return SAVE_NONE;
}

/* ---- ETCH Game Boy saves (read only) ---------------------------------------
 * The .sav is the 32 KB SRAM: slot s at (s / 2) * 8 KB + (s & 1) * 4 KB,
 * 10-byte header "ETCH" | version | 0 | size LE16 | Fletcher-16 LE16. */
#define GB_HDR 10
static uint8_t gb_check(uint8_t s, uint32_t *size_out)
{
    uint8_t h[GB_HDR];
    uint32_t off = (uint32_t)(s >> 1) * 8192u + (uint32_t)(s & 1) * 4096u, size;
    if (s >= GB_SLOTS || !rd(storage_gb_path, off, h, GB_HDR)) return SLOT_EMPTY;
    if (h[0] != 'E' || h[1] != 'T' || h[2] != 'C' || h[3] != 'H') return SLOT_EMPTY;
    size = (uint32_t)(h[6] | (h[7] << 8));
    if (!((h[4] == 2 && size == sizeof(project_t)) || (h[4] == 1 && size == PROJECT_V1_SIZE))) return SLOT_BAD;
    if (!rd(storage_gb_path, off + GB_HDR, buf, size)) return SLOT_BAD;
    if (etch_fletcher16(buf, size) != (uint16_t)(h[8] | (h[9] << 8))) return SLOT_BAD;
    *size_out = size;
    return SLOT_OK;
}

uint8_t storage_gb_status(uint8_t slot, char *name10)
{
    uint32_t size;
    uint8_t r = gb_check(slot, &size);
    if (r == SLOT_OK && name10) memcpy(name10, buf, PROJECT_NAME_LEN);
    return r;
}

uint8_t storage_gb_load(uint8_t slot, project_t *p)
{
    uint32_t size;
    if (gb_check(slot, &size) != SLOT_OK) return 0;
    memset(p, 0, sizeof *p);
    memcpy(p, buf, size);
    if (size < sizeof(project_t)) project_drum_defaults(p);       /* ETCH v1 slot */
    return 1;
}
