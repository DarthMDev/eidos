/*
 * engine/world/bmap_spots.h -- where each battle stands in the one-screen
 * battle backdrop (bmap_spots.c, docs/battle-backdrop.md).
 */

#ifndef EIDOS_ENGINE_WORLD_BMAP_SPOTS_H
#define EIDOS_ENGINE_WORLD_BMAP_SPOTS_H

#include "bmap_place.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    BMAP_SPOT_AUTO,     /* the search from where the player stands          */
    BMAP_SPOT_FIXED,    /* one spot and angle, whatever the player's place   */
    BMAP_SPOT_ZONE      /* the search kept in a safe rectangle; the angle the
                           player's facing or a fixed one                    */
};

/* One row of the table: a trainer ('T') or a wild area ('A'). */
typedef struct BmapSpot {
    char id[16];            /* T<zone>.<npc> or A<zone>.<n>                  */
    char kind;
    unsigned zone;
    char label[80];
    int tx, tz, dir, range; /* T: the trainer's tile, its facing (0 up, 1 down,
                               2 left, 3 right) and sight range (0: talk only) */
    int rx0, rz0, rx1, rz1; /* A: the encounter tiles' box (tiles, inclusive)  */
    float px, pz;           /* where a player naturally is when the battle
                               starts (map units) ...                          */
    int pf;                 /* ... and the way they face (degrees: 0 north,
                               90 west, 180 south, 270 east)                   */
    int mode;               /* BMAP_SPOT_*                                     */
    float sx, sz;           /* FIXED: the own Pokemon's place (map units)      */
    float angle;            /* FIXED, ZONE: the facing in degrees; ZONE -1: the
                               player's                                        */
    int ax0, az0, ax1, az1; /* ZONE: the safe rectangle (tiles, inclusive)     */
    int ok;                 /* checked by eye in the spot editor               */
    int foe_level;          /* 1: the ground levelled between the two Pokemon;
                               0: each on its own ground (the foe's slot moves) */
    float foe_dz;           /* the foe pushed back (battle units; negative:
                               closer); the move animations follow             */
    BmapIntro intro;        /* the intro's camera, apart from the command
                               camera (bmap_camera_intro; zoom 1 none)         */
    int trainer_id;         /* T: the trainer's number (its script - 3000), -1 when
                               it has none (a leader's scripted battle)        */
} BmapSpot;

/* The table: the one built in (battle_spots.tsv), then the rows of the
 * user's file over it (the spot editor writes it).  Loaded on first use. */
void      bmap_spots_set_user_path(const char *path);
const char *bmap_spots_user_path(void);
int       bmap_spots_count(void);
BmapSpot *bmap_spots_get(int i);
/* The row for a battle that starts at (px, pz) map units in `zone`: a
 * trainer row whose sight line or neighbouring tiles hold the player, or a
 * wild area row whose box holds them.  -1 when none. */
int       bmap_spots_find(unsigned zone, int trainer, float px, float pz);
/* The placement for row e (NULL: none) and the player at (px, pz), facing
 * the game's 16-bit angle. */
void      bmap_spots_spec(const BmapSpot *e, float px, float pz, unsigned facing,
                          BmapPlaceSpec *out);
/* The built-in row `id` (before the user's file), into *out.  Returns 0
 * when there is one. */
int       bmap_spots_default(const char *id, BmapSpot *out);
/* Write the whole table to the user's file.  Returns 0 on success. */
int       bmap_spots_save(void);

#ifdef __cplusplus
}
#endif

#endif
