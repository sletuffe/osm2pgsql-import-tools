/*
 * expiremeta-fast.c — drop-in replacement for expiremeta.pl
 *
 * Reads osm2pgsql expire tile list from stdin (z/x/y lines),
 * deduplicates metatile ancestors, then sets their mtime far in the
 * past so mod_tile/tirex will re-render them on next request.
 *
 * Compile:  gcc -O2 -o expiremeta-fast expiremeta-fast.c
 * Usage:    expiremeta-fast --map=openhikingmap [--minzoom=13] \
 *                           [--config=/etc/tirex] [--timeoffset=8000]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <sys/stat.h>
#include <utime.h>
#include <time.h>

/* ── Hash set of uint64_t ──────────────────────────────────────── */

#define HS_EMPTY UINT64_MAX

typedef struct {
    uint64_t *table;
    size_t    mask;   /* capacity-1, capacity is always a power of 2 */
    size_t    count;
} HashSet;

static void hs_alloc(HashSet *hs, unsigned bits) {
    size_t cap  = (size_t)1 << bits;
    hs->table   = malloc(cap * sizeof(uint64_t));
    if (!hs->table) { perror("malloc"); exit(1); }
    memset(hs->table, 0xFF, cap * sizeof(uint64_t)); /* fill with UINT64_MAX */
    hs->mask    = cap - 1;
    hs->count   = 0;
}

static uint64_t hs_hash(uint64_t v) {
    v ^= v >> 33;
    v *= UINT64_C(0xff51afd7ed558ccd);
    v ^= v >> 33;
    v *= UINT64_C(0xc4ceb9fe1a85ec53);
    v ^= v >> 33;
    return v;
}

static void hs_grow(HashSet *hs) {
    HashSet old = *hs;
    hs_alloc(hs, 1 + __builtin_ctzll(old.mask + 1) + 1); /* double capacity */
    for (size_t i = 0; i <= old.mask; i++) {
        if (old.table[i] == HS_EMPTY) continue;
        uint64_t v = old.table[i];
        size_t h = hs_hash(v) & hs->mask;
        while (hs->table[h] != HS_EMPTY) h = (h + 1) & hs->mask;
        hs->table[h] = v;
        hs->count++;
    }
    free(old.table);
}

/* Returns 1 if newly inserted, 0 if already present */
static int hs_insert(HashSet *hs, uint64_t val) {
    if (hs->count * 10 > (hs->mask + 1) * 7) /* load > 70 % */
        hs_grow(hs);
    size_t h = hs_hash(val) & hs->mask;
    while (hs->table[h] != HS_EMPTY) {
        if (hs->table[h] == val) return 0;
        h = (h + 1) & hs->mask;
    }
    hs->table[h] = val;
    hs->count++;
    return 1;
}

/* ── Encode/decode (z, mx, my) in a single uint64 ─────────────── */
/* z  : 6 bits  (values 0-63)
   mx : 25 bits (values 0-2^25, covers metatile coords up to zoom 22)
   my : 25 bits
   Total: 56 bits — never equals UINT64_MAX (all 64 bits set)       */

static inline uint64_t encode(int z, int mx, int my) {
    return ((uint64_t)z << 50) | ((uint64_t)mx << 25) | (uint64_t)my;
}

static inline void decode(uint64_t v, int *z, int *mx, int *my) {
    *z  = (int)(v >> 50);
    *mx = (int)((v >> 25) & 0x1FFFFFF);
    *my = (int)(v & 0x1FFFFFF);
}

/* ── Metatile path (Tirex format, verified on live instance) ────── */
/* Path: tiledir/z/h4/h3/h2/h1/h0.meta
   Each hi = ((x & 0xF) << 4) | (y & 0xF), built low-to-high       */

static void metatile_path(char *buf, const char *tiledir, int z, int mx, int my) {
    int hash[5], x = mx, y = my;
    for (int i = 0; i < 5; i++) {
        hash[i] = ((x & 0xF) << 4) | (y & 0xF);
        x >>= 4; y >>= 4;
    }
    sprintf(buf, "%s/%d/%d/%d/%d/%d/%d.meta",
            tiledir, z, hash[4], hash[3], hash[2], hash[1], hash[0]);
}

/* ── Parse tiledir from Tirex map config ─────────────────────────
   Format: tiledir=/some/path  (key=value, optional spaces, # comments) */

static int get_tiledir(const char *configdir, const char *mapname,
                       char *tiledir, size_t maxlen) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/renderer/mapnik/%s.conf", configdir, mapname);
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "Cannot open %s\n", path); return 0; }

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        if (strncmp(line, "tiledir", 7) != 0) continue;
        char *p = strchr(line, '=');
        if (!p) continue;
        p++;
        while (*p == ' ' || *p == '\t') p++;
        size_t l = strlen(p);
        while (l > 0 && (p[l-1] == '\n' || p[l-1] == '\r' ||
                         p[l-1] == ' '  || p[l-1] == '\t')) l--;
        while (l > 1 && p[l-1] == '/') l--;  /* strip trailing slash */
        snprintf(tiledir, maxlen, "%.*s", (int)l, p);
        fclose(f);
        return 1;
    }
    fclose(f);
    fprintf(stderr, "tiledir not found for map '%s'\n", mapname);
    return 0;
}

/* ── Main ────────────────────────────────────────────────────────── */

int main(int argc, char *argv[]) {
    const char *mapname    = NULL;
    int         minz       = 13;
    const char *configdir  = "/etc/tirex";
    int         timeoffset = 8000; /* days, ~22 years */

    for (int i = 1; i < argc; i++) {
        if      (strncmp(argv[i], "--map=",        6)  == 0) mapname    = argv[i] + 6;
        else if (strncmp(argv[i], "--minzoom=",    10) == 0) minz       = atoi(argv[i] + 10);
        else if (strncmp(argv[i], "--config=",     9)  == 0) configdir  = argv[i] + 9;
        else if (strncmp(argv[i], "--timeoffset=", 13) == 0) timeoffset = atoi(argv[i] + 13);
    }

    if (!mapname) {
        fprintf(stderr,
                "usage: %s --map=MAPNAME [--minzoom=Z] [--config=DIR] [--timeoffset=DAYS]\n",
                argv[0]);
        return 1;
    }

    char tiledir[1024];
    if (!get_tiledir(configdir, mapname, tiledir, sizeof(tiledir)))
        return 1;

    /* ── Pass 1 : read expire list, insert all unique ancestors ─── */

    HashSet hs;
    hs_alloc(&hs, 23); /* start at 8M slots = 64 MB */

    char line[64];
    long reported = 0;

    while (fgets(line, sizeof(line), stdin)) {
        int z, x, y;
        if (sscanf(line, "%d/%d/%d", &z, &x, &y) != 3) continue;
        reported++;

        /* Tirex metatile coordinate space: same transform as original Perl
           touch_with_recurse($x<<3, $y<<3, $z+3, 0)                        */
        int mx = x << 3, my = y << 3, mz = z + 3;

        while (mz >= minz) {
            if (!hs_insert(&hs, encode(mz, mx, my)))
                break; /* already present → all parents already in set */
            mz--;
            mx = (mx >> 4) << 3;
            my = (my >> 4) << 3;
        }
    }

    /* ── Pass 2 : utime() each unique metatile that exists on disk ─ */

    time_t         t  = time(NULL) - (time_t)timeoffset * 86400;
    struct utimbuf ut = { .actime = t, .modtime = t };
    char           path[2048];
    struct stat    st;
    long           touched = 0, nonex = 0;

    for (size_t i = 0; i <= hs.mask; i++) {
        if (hs.table[i] == HS_EMPTY) continue;
        int z, mx, my;
        decode(hs.table[i], &z, &mx, &my);
        metatile_path(path, tiledir, z, mx, my);
        if (stat(path, &st) == 0) {
            utime(path, &ut);
            touched++;
        } else {
            nonex++;
        }
    }

    fprintf(stderr,
            "%ld tiles reported, %zu unique metatiles, %ld did not exist, %ld touched\n",
            reported, hs.count, nonex, touched);

    free(hs.table);
    return 0;
}
