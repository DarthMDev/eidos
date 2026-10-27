/*
 * tests/test_bmap_spots.c -- the battle spot table (engine/world/bmap_spots.c).
 *
 * No game data: the table is built into the program.  The checks:
 *
 *   - the built-in table loads, with rows for trainers and wild areas;
 *   - a battle finds its row: a trainer's from a tile of its sight line or
 *     beside it (not past its range), a wild area's from a tile in its box;
 *   - each mode gives its placement: auto faces the trainer with no turn,
 *     fixed takes the row's spot and angle, a safe area keeps the search in
 *     its rectangle and follows the player's facing unless an angle is set;
 *   - the foe's distance, its ground and the intro zoom reach the placement;
 *   - a change is written to the user's file and read back over the
 *     built-in row, and the built-in row stays available as the default.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bmap_spots.h"
#include "port_test.h"

static float tile_centre(int t)
{
    return (float)t * 16.0f + 8.0f;
}

static int row_of(const char *id)
{
    int i;

    for (i = 0; i < bmap_spots_count(); i++) {
        if (strcmp(bmap_spots_get(i)->id, id) == 0) {
            return i;
        }
    }
    return -1;
}

static const char *found(unsigned zone, int trainer, int tx, int tz)
{
    BmapSpot *r = bmap_spots_get(bmap_spots_find(zone, trainer, tile_centre(tx), tile_centre(tz)));

    return r != NULL ? r->id : "none";
}

int main(void)
{
    const char *path = "test_bmap_spots_user.tsv";
    int waiter, area, trainers = 0, wild = 0, i;
    BmapSpot *r, d;
    BmapPlaceSpec sp;
    FILE *f;
    char line[512];
    int seen = 0;

    remove(path);
    bmap_spots_set_user_path(path);

    /* the table */
    for (i = 0; i < bmap_spots_count(); i++) {
        trainers += bmap_spots_get(i)->kind == 'T';
        wild += bmap_spots_get(i)->kind == 'A';
    }
    BW_REQUIRE("rows for trainers", trainers > 300);
    BW_REQUIRE("rows for wild areas", wild > 300);
    waiter = row_of("T7.7");            /* Striaton Gym's waiter: tile (9, 30), facing right, sight 6 */
    area = row_of("A317.4");            /* Route 1, area 4: tiles (784, 715) to (791, 723) */
    BW_REQUIRE("T7.7 is in the table", waiter >= 0);
    BW_REQUIRE("A317.4 is in the table", area >= 0);
    r = bmap_spots_get(waiter);
    BW_CHECK_EQ("T7.7 tile x", r->tx, 9);
    BW_CHECK_EQ("T7.7 sight", r->range, 6);

    /* finding a battle's row */
    BW_CHECK_EQ("sight line, 2 tiles", strcmp(found(7, 1, 11, 30), "T7.7"), 0);
    BW_CHECK_EQ("sight line, 6 tiles", strcmp(found(7, 1, 15, 30), "T7.7"), 0);
    BW_CHECK_EQ("past the range", strcmp(found(7, 1, 16, 30), "T7.7") != 0, 1);
    BW_CHECK_EQ("beside it (talk)", strcmp(found(7, 1, 9, 29), "T7.7"), 0);
    BW_CHECK_EQ("off the line", strcmp(found(7, 1, 12, 32), "T7.7") != 0, 1);
    BW_CHECK_EQ("wild, in the box", strcmp(found(317, 0, 786, 718), "A317.4"), 0);
    BW_CHECK_EQ("wild, other zone", strcmp(found(318, 0, 786, 718), "none"), 0);
    BW_CHECK_EQ("a trainer row is not a wild one", strcmp(found(7, 0, 11, 30), "T7.7") != 0, 1);

    /* auto: from the player, facing the trainer (west of the player), no turn */
    r->mode = BMAP_SPOT_AUTO;
    bmap_spots_spec(r, tile_centre(11), tile_centre(30), 0x0000u, &sp);
    BW_CHECK_EQ("auto: the player's x", (long)sp.px, (long)tile_centre(11));
    BW_CHECK_EQ("auto: faces the trainer", sp.facing, 0x4000);
    BW_CHECK_EQ("auto: no turn", sp.turn, 0);
    BW_CHECK_EQ("auto: a trainer's reach", sp.reach, BMAP_PLACE_REACH_TRAINER);
    BW_CHECK_EQ("auto: levelled", sp.foe_level, 1);

    /* fixed */
    r->mode = BMAP_SPOT_FIXED;
    r->sx = 200.0f;
    r->sz = 400.0f;
    r->angle = 180.0f;
    r->foe_dz = 2.5f;
    r->foe_level = 0;
    r->intro.zoom = 1.5f;
    bmap_spots_spec(r, tile_centre(11), tile_centre(30), 0x0000u, &sp);
    BW_CHECK_EQ("fixed: spot x", (long)sp.px, 200);
    BW_CHECK_EQ("fixed: spot z", (long)sp.pz, 400);
    BW_CHECK_EQ("fixed: angle", sp.facing, 0x8000);
    BW_CHECK_EQ("fixed: as given", sp.reach, -1);
    BW_CHECK_EQ("fixed: foe distance", (long)(sp.foe_dz * 10.0f), 25);
    BW_CHECK_EQ("fixed: foe on its ground", sp.foe_level, 0);
    BW_CHECK_EQ("fixed: intro zoom", (long)(sp.intro.zoom * 10.0f), 15);

    /* a safe area: the rectangle, the player's facing, then a set angle */
    r = bmap_spots_get(area);
    r->mode = BMAP_SPOT_ZONE;
    r->ax0 = 785;
    r->az0 = 716;
    r->ax1 = 788;
    r->az1 = 719;
    r->angle = -1.0f;
    bmap_spots_spec(r, tile_centre(800), tile_centre(700), 0xC000u, &sp);
    BW_CHECK_EQ("area: a rectangle", sp.has_rect, 1);
    BW_CHECK_EQ("area: rectangle x0", (long)sp.x0, (long)tile_centre(785));
    BW_CHECK_EQ("area: rectangle z1", (long)sp.z1, (long)tile_centre(719));
    BW_CHECK_EQ("area: the player's facing", sp.facing, 0xC000);
    BW_CHECK_EQ("area: may turn", sp.turn, 1);
    r->angle = 90.0f;
    bmap_spots_spec(r, tile_centre(800), tile_centre(700), 0xC000u, &sp);
    BW_CHECK_EQ("area: a set angle", sp.facing, 0x4000);
    BW_CHECK_EQ("area: no turn", sp.turn, 0);

    /* no row: the search from the player, as they face */
    bmap_spots_spec(NULL, 10.0f, 20.0f, 0x8000u, &sp);
    BW_CHECK_EQ("no row: facing", sp.facing, 0x8000);
    BW_CHECK_EQ("no row: reach", sp.reach, BMAP_PLACE_REACH);
    BW_CHECK_EQ("no row: no rectangle", sp.has_rect, 0);

    /* saving: the user's file has the changed rows, the default stays */
    BW_REQUIRE("save", bmap_spots_save() == 0);
    f = fopen(path, "r");
    BW_REQUIRE("the user's file", f != NULL);
    while (fgets(line, sizeof line, f) != NULL) {
        if (strncmp(line, "T7.7\t", 5) == 0) {
            seen = strstr(line, "\tfixed\t200\t400\t180\t") != NULL;
        }
    }
    fclose(f);
    BW_CHECK_EQ("the saved row", seen, 1);
    BW_REQUIRE("the default row", bmap_spots_default("T7.7", &d) == 0);
    BW_CHECK_EQ("the default is untouched", d.sx == 200.0f && d.sz == 400.0f, 0);
    remove(path);
    BW_REPORT("test_bmap_spots");
}
