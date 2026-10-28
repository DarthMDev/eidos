/*
 * engine/world/bmap_spots.c -- where each battle stands in the one-screen
 * battle backdrop (docs/battle-backdrop.md).
 *
 * The table has a row for every place a battle can start: each trainer
 * (its sight line, or the tiles beside it for one the player talks to) and
 * each patch of wild encounter tiles.  tools/battle_spots.py builds it from
 * the game's data (the NPC records, the walk grid's encounter flag) into
 * battle_spots.tsv, built in through battle_spots_tsv.inc.  A row says how
 * its battle is placed:
 *
 *   auto   the search (bmap_place_spec) from the player's own place; the
 *          foe stays where the player looks (45 degrees off at most)
 *   fixed  one spot and angle, chosen by eye in the spot editor
 *   zone   the search kept inside a safe rectangle chosen by eye, with the
 *          player's facing or a fixed angle
 *
 * The spot editor (tools/spot-editor/eidos_spots.c) changes rows and writes
 * the whole table to the user's file, which is read over the built-in rows
 * at the next start; tools/battle_spots.py merge takes it back into the
 * source.  A battle with no row is placed the auto way.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bmap_spots.h"

static const char k_builtin[] =
#include "battle_spots_tsv.inc"
    ;

static BmapSpot *g_rows;
static int g_n, g_cap, g_loaded;
static char g_user[512];

static int field_int(const char *f, int dflt)
{
    return (f[0] == '-' && f[1] == '\0') || f[0] == '\0' ? dflt : atoi(f);
}

static float field_float(const char *f, float dflt)
{
    return (f[0] == '-' && f[1] == '\0') || f[0] == '\0' ? dflt : (float)atof(f);
}

static int mode_of(const char *f)
{
    return strcmp(f, "fixed") == 0 ? BMAP_SPOT_FIXED : strcmp(f, "zone") == 0 ? BMAP_SPOT_ZONE
                                                                                : BMAP_SPOT_AUTO;
}

/* One line of the table into r; returns 0 when it is a row. */
static int parse_row(char *line, BmapSpot *r)
{
    char *f[33];
    int n = 0;
    char *p = line;

    if (line[0] == '#' || line[0] == '\0') {
        return -1;
    }
    while (n < 33) {
        f[n++] = p;
        p = strchr(p, '\t');
        if (p == NULL) {
            break;
        }
        *p++ = '\0';
    }
    if (n < 24) {
        return -1;
    }
    memset(r, 0, sizeof *r);
    snprintf(r->id, sizeof r->id, "%s", f[0]);
    r->kind = f[1][0];
    r->zone = (unsigned)field_int(f[2], 0);
    snprintf(r->label, sizeof r->label, "%s", f[3]);
    r->tx = field_int(f[4], 0);
    r->tz = field_int(f[5], 0);
    r->dir = field_int(f[6], 1);
    r->range = field_int(f[7], 0);
    r->rx0 = field_int(f[8], 0);
    r->rz0 = field_int(f[9], 0);
    r->rx1 = field_int(f[10], -1);
    r->rz1 = field_int(f[11], -1);
    r->px = field_float(f[12], 0.0f);
    r->pz = field_float(f[13], 0.0f);
    r->pf = field_int(f[14], 0);
    r->mode = mode_of(f[15]);
    r->sx = field_float(f[16], r->px);
    r->sz = field_float(f[17], r->pz);
    r->angle = field_float(f[18], -1.0f);
    r->ax0 = field_int(f[19], r->rx0);
    r->az0 = field_int(f[20], r->rz0);
    r->ax1 = field_int(f[21], r->rx1);
    r->az1 = field_int(f[22], r->rz1);
    r->ok = field_int(f[23], 0);
    r->foe_level = n > 24 ? field_int(f[24], 1) : 1;
    r->foe_dz = n > 25 ? field_float(f[25], 0.0f) : 0.0f;
    r->intro.zoom = n > 26 ? field_float(f[26], 1.0f) : 1.0f;
    r->trainer_id = n > 27 ? field_int(f[27], -1) : -1;
    r->intro.yaw = n > 28 ? field_float(f[28], 0.0f) : 0.0f;
    r->intro.lift = n > 29 ? field_float(f[29], 0.0f) : 0.0f;
    r->intro.side = n > 30 ? field_float(f[30], 0.0f) : 0.0f;
    r->intro.tilt = n > 31 ? field_float(f[31], 0.0f) : 0.0f;
    r->intro.aim = n > 32 ? field_float(f[32], 0.0f) : 0.0f;
    if (r->intro.zoom <= 0.0f) {
        r->intro.zoom = 1.0f;
    }
    return 0;
}

static int find_id(const char *id)
{
    int i;

    for (i = 0; i < g_n; i++) {
        if (strcmp(g_rows[i].id, id) == 0) {
            return i;
        }
    }
    return -1;
}

/* Every row of text into the table; a row with an id already there
 * replaces it. */
static void load_text(char *text)
{
    char *line = text;

    while (line != NULL && *line != '\0') {
        char *next = strchr(line, '\n');
        BmapSpot r;
        int at;

        if (next != NULL) {
            *next++ = '\0';
        }
        if (next != NULL && next - line >= 2 && next[-2] == '\r') {
            next[-2] = '\0';
        }
        if (parse_row(line, &r) == 0) {
            at = find_id(r.id);
            if (at >= 0) {
                if (r.trainer_id < 0) {
                    r.trainer_id = g_rows[at].trainer_id;   /* a file from before the column */
                }
                g_rows[at] = r;
            } else {
                if (g_n == g_cap) {
                    int cap = g_cap > 0 ? g_cap * 2 : 1024;
                    BmapSpot *m = (BmapSpot *)realloc(g_rows, (size_t)cap * sizeof *m);

                    if (m == NULL) {
                        return;
                    }
                    g_rows = m;
                    g_cap = cap;
                }
                g_rows[g_n++] = r;
            }
        }
        line = next;
    }
}

static void load(void)
{
    char *text;
    FILE *f;

    if (g_loaded) {
        return;
    }
    g_loaded = 1;
    text = (char *)malloc(sizeof k_builtin);
    if (text != NULL) {
        memcpy(text, k_builtin, sizeof k_builtin);
        load_text(text);
        free(text);
    }
    if (bmap_spots_user_path()[0] != '\0' && (f = fopen(bmap_spots_user_path(), "rb")) != NULL) {
        long n;

        fseek(f, 0, SEEK_END);
        n = ftell(f);
        fseek(f, 0, SEEK_SET);
        text = n > 0 ? (char *)malloc((size_t)n + 1u) : NULL;
        if (text != NULL && fread(text, 1, (size_t)n, f) == (size_t)n) {
            text[n] = '\0';
            load_text(text);
        }
        free(text);
        fclose(f);
    }
}

void bmap_spots_set_user_path(const char *path)
{
    snprintf(g_user, sizeof g_user, "%s", path != NULL ? path : "");
}

const char *bmap_spots_user_path(void)
{
    if (getenv("EIDOS_BATTLE_SPOTS") != NULL) {
        return getenv("EIDOS_BATTLE_SPOTS");
    }
    return g_user;
}

int bmap_spots_default(const char *id, BmapSpot *out)
{
    char *text = (char *)malloc(sizeof k_builtin), *line;
    int found = -1;

    if (text == NULL) {
        return -1;
    }
    memcpy(text, k_builtin, sizeof k_builtin);
    for (line = text; line != NULL && *line != '\0' && found != 0;) {
        char *next = strchr(line, '\n');
        BmapSpot r;

        if (next != NULL) {
            *next++ = '\0';
        }
        if (parse_row(line, &r) == 0 && strcmp(r.id, id) == 0) {
            *out = r;
            found = 0;
        }
        line = next;
    }
    free(text);
    return found;
}

int bmap_spots_count(void)
{
    load();
    return g_n;
}

BmapSpot *bmap_spots_get(int i)
{
    load();
    return i >= 0 && i < g_n ? &g_rows[i] : NULL;
}

/* A trainer battle's row by the trainer's number (story battles start
 * wherever the player stands, not on a sight line): among rows of that
 * number in the zone, the one nearest the player.  trainer_id 0: the
 * nearest story row (sight range 0) in the zone instead, the last resort
 * after the sight-line search (a rival's number changes with the starter
 * the player chose; the place does not).  -1: none. */
int bmap_spots_find_trainer(unsigned zone, int trainer_id, float px, float pz)
{
    int i, best = -1, best_story = -1;
    float bd = 1e30f, bs = 1e30f;

    load();
    for (i = 0; i < g_n; i++) {
        const BmapSpot *r = &g_rows[i];
        float dx, dz, d;

        if (r->zone != zone || r->kind != 'T') {
            continue;
        }
        dx = px - ((float)r->tx * 16.0f + 8.0f);
        dz = pz - ((float)r->tz * 16.0f + 8.0f);
        d = dx * dx + dz * dz;
        if (trainer_id > 0 && r->trainer_id == trainer_id && d < bd) {
            bd = d;
            best = i;
        }
        if (trainer_id <= 0 && r->range <= 0 && d < bs) {
            bs = d;
            best_story = i;
        }
    }
    return trainer_id > 0 ? best : best_story;
}

int bmap_spots_find(unsigned zone, int trainer, float px, float pz)
{
    int tx = (int)floorf(px / 16.0f), tz = (int)floorf(pz / 16.0f);
    int i, best = -1;
    float bd = 1e30f;

    load();
    for (i = 0; i < g_n; i++) {
        const BmapSpot *r = &g_rows[i];
        float d;

        if (r->zone != zone || r->kind != (trainer ? 'T' : 'A')) {
            continue;
        }
        if (trainer) {
            /* the player on the trainer's sight line (in any direction: some
             * trainers look around; their own facing first) or beside it */
            int dx = tx - r->tx, dz = tz - r->tz, ad = abs(dx) + abs(dz);
            int rng = r->range > 0 ? r->range : 1;
            static const int dv[4][2] = { { 0, -1 }, { 0, 1 }, { -1, 0 }, { 1, 0 } };

            if (ad == 0 || (dx != 0 && dz != 0) || ad > rng) {
                continue;
            }
            d = (float)ad;
            if (r->dir < 0 || r->dir > 3 || dx * dv[r->dir][0] + dz * dv[r->dir][1] <= 0) {
                d += 0.5f;
            }
        } else {
            /* the player in the area's box (a tile of slack: the step that
             * started the battle may end on its edge) */
            if (tx < r->rx0 - 1 || tx > r->rx1 + 1 || tz < r->rz0 - 1 || tz > r->rz1 + 1) {
                continue;
            }
            d = (float)((r->rx1 - r->rx0 + 1) * (r->rz1 - r->rz0 + 1));   /* the smallest box */
        }
        if (d < bd) {
            bd = d;
            best = i;
        }
    }
    return best;
}

static unsigned deg_to_facing(float deg)
{
    double d = fmod((double)deg, 360.0);

    if (d < 0.0) {
        d += 360.0;
    }
    return (unsigned)(d * 65536.0 / 360.0 + 0.5) & 0xFFFFu;
}

void bmap_spots_spec(const BmapSpot *e, float px, float pz, unsigned facing, BmapPlaceSpec *out)
{
    memset(out, 0, sizeof *out);
    out->px = px;
    out->pz = pz;
    out->facing = facing;
    out->reach = BMAP_PLACE_REACH;
    out->foe_level = 1;                 /* the ground levelled, the game's heights */
    out->turn = 1;
    if (e == NULL) {
        return;
    }
    out->foe_level = e->foe_level;
    out->foe_dz = e->foe_dz;
    out->intro = e->intro;
    if (e->kind == 'T') {
        /* the player faces the trainer, wherever the game left the facing */
        float vx = (float)e->tx * 16.0f + 8.0f - px, vz = (float)e->tz * 16.0f + 8.0f - pz;

        if (vx * vx + vz * vz > 1.0f) {
            out->facing = deg_to_facing((float)(atan2(-(double)vx, -(double)vz) * 180.0 / 3.14159265358979323846));
        }
        out->turn = 0;                  /* the foe exactly where the trainer is */
        out->reach = BMAP_PLACE_REACH_TRAINER;  /* room to back off from a stage or a desk */
    }
    if (e->kind != 'T' && e->mode == BMAP_SPOT_AUTO && e->rx1 >= e->rx0 && e->rz1 >= e->rz0) {
        out->has_area = 1;
        out->area[0] = (float)e->rx0 * 16.0f;
        out->area[1] = (float)e->rz0 * 16.0f;
        out->area[2] = (float)e->rx1 * 16.0f + 16.0f;
        out->area[3] = (float)e->rz1 * 16.0f + 16.0f;
    }
    switch (e->mode) {
    case BMAP_SPOT_FIXED:
        out->px = e->sx;
        out->pz = e->sz;
        out->facing = e->angle >= 0.0f ? deg_to_facing(e->angle) : out->facing;
        out->reach = -1;
        out->turn = 0;
        break;
    case BMAP_SPOT_ZONE:
        out->has_rect = 1;
        out->x0 = (float)e->ax0 * 16.0f + 8.0f;
        out->z0 = (float)e->az0 * 16.0f + 8.0f;
        out->x1 = (float)e->ax1 * 16.0f + 8.0f;
        out->z1 = (float)e->az1 * 16.0f + 8.0f;
        if (e->angle >= 0.0f) {
            out->facing = deg_to_facing(e->angle);
            out->turn = 0;
        }
        break;
    default:
        break;
    }
}

static const char *mode_name(int m)
{
    return m == BMAP_SPOT_FIXED ? "fixed" : m == BMAP_SPOT_ZONE ? "zone" : "auto";
}

int bmap_spots_save(void)
{
    FILE *f;
    int i;

    load();
    if (bmap_spots_user_path()[0] == '\0' || (f = fopen(bmap_spots_user_path(), "wb")) == NULL) {
        return -1;
    }
    fprintf(f, "# battle spots written by the spot editor: tools/battle_spots.py merge\n");
    for (i = 0; i < g_n; i++) {
        const BmapSpot *r = &g_rows[i];

        if (r->kind == 'T') {
            fprintf(f, "%s\tT\t%u\t%s\t%d\t%d\t%d\t%d\t-\t-\t-\t-", r->id, r->zone, r->label, r->tx, r->tz,
                    r->dir, r->range);
        } else {
            fprintf(f, "%s\tA\t%u\t%s\t-\t-\t-\t-\t%d\t%d\t%d\t%d", r->id, r->zone, r->label, r->rx0, r->rz0,
                    r->rx1, r->rz1);
        }
        fprintf(f, "\t%g\t%g\t%d\t%s", (double)r->px, (double)r->pz, r->pf, mode_name(r->mode));
        if (r->mode == BMAP_SPOT_FIXED) {
            fprintf(f, "\t%g\t%g\t%g\t-\t-\t-\t-", (double)r->sx, (double)r->sz, (double)r->angle);
        } else if (r->mode == BMAP_SPOT_ZONE) {
            fprintf(f, "\t-\t-\t%g\t%d\t%d\t%d\t%d", (double)r->angle, r->ax0, r->az0, r->ax1, r->az1);
        } else {
            fprintf(f, "\t-\t-\t-\t-\t-\t-\t-");
        }
        fprintf(f, "\t%d\t%d\t%g\t%g\t%d\t%g\t%g\t%g\t%g\t%g\n", r->ok, r->foe_level, (double)r->foe_dz,
                (double)r->intro.zoom, r->trainer_id, (double)r->intro.yaw, (double)r->intro.lift,
                (double)r->intro.side, (double)r->intro.tilt, (double)r->intro.aim);
    }
    return fclose(f) == 0 ? 0 : -1;
}
