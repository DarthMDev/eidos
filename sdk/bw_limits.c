/*
 * sdk/bw_limits.c -- the game's content limits, raised by mods (bw_limits.h).
 *
 * Two halves.
 *
 * The numbers.  Each limit aletheia names in src/game/port_limits.h has a
 * row in k_limits: its vanilla value (the literal the DS build keeps), the
 * manifest key a mod raises it with, and the ceiling the game's storage
 * allows (an item id is a u16 everywhere it is stored, and 0xFFFF is the
 * "none" value some lists end with).  bw_limits_apply reads every enabled
 * mod's manifest; bw_limit_get answers the highest value, never below the
 * vanilla one.
 *
 * The tables.  A table with one row per id lives in the game's own data,
 * sized for the vanilla count, and the game indexes it with the id.
 * Raising the limit alone would make the game read past its end.  The
 * port builds a copy with a row for every id up to the limit and points
 * the game's name at it: the game's code reads `item_gfx_member[item]`
 * through the name's reference (sdk/armdata.h, bw_dsref_<name>), so a
 * longer copy is all it takes.  Rows past the vanilla ones start as a copy
 * of a default row (row 0, the game's own "nothing" entry) and take the
 * values mods give in eidos/tables/<table>.txt.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "armdata.h"
#include "bw_limits.h"
#include "port.h"
#include "vfs.h"

typedef struct LimitRow {
    const char *key;        /* the manifest key                           */
    u32         vanilla;    /* the value the DS build has                 */
    u32         ceiling;    /* the most the game's storage holds          */
} LimitRow;

static const LimitRow k_limits[BW_PORT_LIMIT_COUNT] = {
    /* ITEM_LAST: item ids are u16; 0xFFFF ends some lists. */
    { "limit.items", 626u, 0xFFFEu },
    /* MOVE_LAST: move ids are u16; 0xFFFF ends the learnsets.  The battle
     * view's effect scripts number the moves below 0x231 and its other
     * effects from 0x231 (ov093 btlv_effvm.c), so a move past 0x230 has
     * no animation until that routing is remapped. */
    { "limit.moves", 559u, 0xFFFEu },
    /* BTLV_EFFECT_MOVE_END: the first battle effect id that is not a move
     * (ov094 btlv_effvm_types.h).  No mod sets it: it is to follow the
     * move limit once every effect id the battle server sends is written
     * relative to it, so it stays at the DS value until then. */
    { NULL, 0x231u, 0x231u },
    /* FLDMMDL_OBJCODE_TABLE_COUNT: the field object code table (ov010
     * fldmmdl_types.h).  Not raisable until the code-to-index remap is in
     * the port. */
    { NULL, 799u, 799u },
};

static u32 g_value[BW_PORT_LIMIT_COUNT];
static int g_have;

static void defaults(void)
{
    int i;

    for (i = 0; i < BW_PORT_LIMIT_COUNT; i++) {
        g_value[i] = k_limits[i].vanilla;
    }
    g_have = 1;
}

u32 bw_limit_get(int id)
{
    if (!g_have) {
        defaults();
    }
    if (id < 0 || id >= BW_PORT_LIMIT_COUNT) {
        return 0;
    }
    return g_value[id];
}

const char *bw_limit_key(int id)
{
    return id >= 0 && id < BW_PORT_LIMIT_COUNT ? k_limits[id].key : NULL;
}

/* ------------------------------------------------------------------ */
/* Reading the mods                                                    */
/* ------------------------------------------------------------------ */

static char *read_text(const char *path)
{
    FILE *f = fopen(path, "rb");
    long n;
    char *buf;

    if (f == NULL) {
        return NULL;
    }
    if (fseek(f, 0, SEEK_END) != 0 || (n = ftell(f)) < 0 || n > (1L << 24) ||
        fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return NULL;
    }
    buf = (char *)malloc((size_t)n + 1);
    if (buf == NULL) {
        fclose(f);
        return NULL;
    }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        fclose(f);
        return NULL;
    }
    buf[n] = '\0';
    fclose(f);
    return buf;
}

/* The next line of `*p` as [*s, *e), comment and blanks trimmed; 0 at the
 * end of the text. */
static int next_line(const char **p, const char **s, const char **e)
{
    const char *a = *p, *b;

    if (*a == '\0') {
        return 0;
    }
    b = a;
    while (*b != '\0' && *b != '\n') {
        b++;
    }
    *p = *b == '\0' ? b : b + 1;
    {
        const char *h = a;

        while (h < b && *h != '#') {
            h++;
        }
        b = h;
    }
    while (a < b && (*a == ' ' || *a == '\t' || *a == '\r')) {
        a++;
    }
    while (b > a && (b[-1] == ' ' || b[-1] == '\t' || b[-1] == '\r')) {
        b--;
    }
    *s = a;
    *e = b;
    return 1;
}

static int parse_u32(const char *s, const char *e, u32 *out)
{
    u32 v = 0;
    int base = 10;

    if (s == e) {
        return -1;
    }
    if (e - s > 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        base = 16;
        s += 2;
    }
    for (; s < e; s++) {
        int d;

        if (*s >= '0' && *s <= '9') {
            d = *s - '0';
        } else if (base == 16 && *s >= 'a' && *s <= 'f') {
            d = *s - 'a' + 10;
        } else if (base == 16 && *s >= 'A' && *s <= 'F') {
            d = *s - 'A' + 10;
        } else {
            return -1;
        }
        if (v > (0xFFFFFFFFu - (u32)d) / (u32)base) {
            return -1;
        }
        v = v * (u32)base + (u32)d;
    }
    *out = v;
    return 0;
}

/* The limits one mod's manifest declares. */
static void read_manifest_limits(const char *root)
{
    char path[1200];
    char *text;
    const char *p, *s, *e;

    if (strlen(root) + 16 > sizeof(path)) {
        return;
    }
    sprintf(path, "%s/eidos_mod.txt", root);
    text = read_text(path);
    if (text == NULL) {
        return;
    }
    p = text;
    while (next_line(&p, &s, &e)) {
        const char *eq = s, *ks, *ke, *vs;
        int id;
        u32 v;

        while (eq < e && *eq != '=') {
            eq++;
        }
        if (eq == e) {
            continue;
        }
        ks = s;
        ke = eq;
        while (ke > ks && (ke[-1] == ' ' || ke[-1] == '\t')) {
            ke--;
        }
        vs = eq + 1;
        while (vs < e && (*vs == ' ' || *vs == '\t')) {
            vs++;
        }
        for (id = 0; id < BW_PORT_LIMIT_COUNT; id++) {
            size_t kl;

            if (k_limits[id].key == NULL) {
                continue;   /* the port sets this one, not a mod */
            }
            kl = strlen(k_limits[id].key);

            if ((size_t)(ke - ks) != kl || memcmp(ks, k_limits[id].key, kl) != 0) {
                continue;
            }
            if (parse_u32(vs, e, &v) != 0) {
                fprintf(stderr, "eidos: mod %s: bad value for %s, ignored\n",
                        root, k_limits[id].key);
            } else if (v > k_limits[id].ceiling) {
                fprintf(stderr, "eidos: mod %s: %s = %u is past the ceiling %u, "
                        "the ceiling is used\n", root, k_limits[id].key,
                        (unsigned)v, (unsigned)k_limits[id].ceiling);
                v = k_limits[id].ceiling;
            }
            if (v > g_value[id]) {
                g_value[id] = v;
            }
        }
    }
    free(text);
}

/* ------------------------------------------------------------------ */
/* The tables                                                          */
/* ------------------------------------------------------------------ */

typedef struct TableSpec {
    const char *name;       /* the game's name (sdk/armdata.h)            */
    const char *file;       /* eidos/tables/<file>.txt                    */
    int         limit;      /* the limit the last row id is               */
    u32         vanilla;    /* rows the game's table has                  */
    u32         row_halves; /* u16 values per row                         */
} TableSpec;

static const TableSpec k_tables[] = {
    /* Per item: the icon's tile member and palette member in a/0/2/5
     * (arm9/file_util.c, FILE_UTIL_Get and FILE_UTIL_LoadItemRecord). */
    { "item_gfx_member", "item_icons", BW_PORT_LIMIT_ITEM_LAST, 627u, 2u },
};

#define TABLE_COUNT (sizeof(k_tables) / sizeof(k_tables[0]))

static u16 *g_table[TABLE_COUNT];

/* Apply one mod's rows for table `t` to `rows` (count of them). */
static void read_table_rows(const char *root, const TableSpec *t, u16 *rows,
                            u32 count)
{
    char path[1200];
    char *text;
    const char *p, *s, *e;
    u32 line = 0;

    if (strlen(root) + strlen(t->file) + 24 > sizeof(path)) {
        return;
    }
    sprintf(path, "%s/eidos/tables/%s.txt", root, t->file);
    text = read_text(path);
    if (text == NULL) {
        return;
    }
    p = text;
    while (next_line(&p, &s, &e)) {
        const char *eq = s, *q;
        u32 id, k, values[8];

        line++;
        if (s == e) {
            continue;
        }
        while (eq < e && *eq != '=') {
            eq++;
        }
        q = eq;
        while (q > s && (q[-1] == ' ' || q[-1] == '\t')) {
            q--;
        }
        if (eq == e || parse_u32(s, q, &id) != 0) {
            fprintf(stderr, "eidos: %s:%u: expected `<id> = <values>`\n",
                    path, (unsigned)line);
            continue;
        }
        q = eq + 1;
        for (k = 0; k < t->row_halves; k++) {
            const char *vs, *ve;

            while (q < e && (*q == ' ' || *q == '\t')) {
                q++;
            }
            vs = q;
            while (q < e && *q != ' ' && *q != '\t') {
                q++;
            }
            ve = q;
            if (parse_u32(vs, ve, &values[k]) != 0 || values[k] > 0xFFFFu) {
                break;
            }
        }
        if (k != t->row_halves) {
            fprintf(stderr, "eidos: %s:%u: expected %u values of 0..65535\n",
                    path, (unsigned)line, (unsigned)t->row_halves);
            continue;
        }
        if (id >= count) {
            fprintf(stderr, "eidos: %s:%u: id %u is past the limit (raise %s)\n",
                    path, (unsigned)line, (unsigned)id,
                    k_limits[t->limit].key);
            continue;
        }
        for (k = 0; k < t->row_halves; k++) {
            rows[id * t->row_halves + k] = (u16)values[k];
        }
    }
    free(text);
}

static int build_table(u32 ti)
{
    const TableSpec *t = &k_tables[ti];
    const BwDataImage *img = bw_data_installed();
    const BwDataName *n;
    const u16 *vanilla;
    u32 count = bw_limit_get(t->limit) + 1u;
    u32 i, m;
    u16 *rows;
    int modded = 0;

    if (img == NULL || (n = bw_data_find(img, t->name)) == NULL ||
        n->ref == NULL || *n->ref == NULL) {
        return 0;               /* the game's table is not in this build */
    }
    vanilla = (const u16 *)*n->ref;
    for (m = 0; m < bw_mod_count(); m++) {
        char path[1200];
        const char *root = bw_mod_root_at(m);

        if (strlen(root) + strlen(t->file) + 24 < sizeof(path)) {
            FILE *f;

            sprintf(path, "%s/eidos/tables/%s.txt", root, t->file);
            if ((f = fopen(path, "rb")) != NULL) {
                fclose(f);
                modded = 1;
            }
        }
    }
    if (count <= t->vanilla && !modded) {
        return 0;               /* the game's own table serves */
    }
    if (count < t->vanilla) {
        count = t->vanilla;
    }
    rows = (u16 *)malloc((size_t)count * t->row_halves * sizeof(u16));
    if (rows == NULL) {
        return -1;
    }
    memcpy(rows, vanilla, (size_t)t->vanilla * t->row_halves * sizeof(u16));
    for (i = t->vanilla; i < count; i++) {
        memcpy(rows + i * t->row_halves, vanilla, t->row_halves * sizeof(u16));
    }
    for (m = 0; m < bw_mod_count(); m++) {
        read_table_rows(bw_mod_root_at(m), t, rows, count);
    }
    free(g_table[ti]);
    g_table[ti] = rows;
    *n->ref = rows;
    return 0;
}

int bw_limits_apply(void)
{
    u32 i;
    int rc = 0;

    defaults();
    for (i = 0; i < bw_mod_count(); i++) {
        read_manifest_limits(bw_mod_root_at(i));
    }
    for (i = 0; i < BW_PORT_LIMIT_COUNT; i++) {
        if (g_value[i] != k_limits[i].vanilla && k_limits[i].key != NULL) {
            fprintf(stderr, "eidos: %s raised to %u\n", k_limits[i].key,
                    (unsigned)g_value[i]);
        }
    }
    for (i = 0; i < TABLE_COUNT; i++) {
        if (build_table(i) != 0) {
            fprintf(stderr, "eidos: the extended table %s could not be built\n",
                    k_tables[i].name);
            rc = -1;
        }
    }
    return rc;
}
