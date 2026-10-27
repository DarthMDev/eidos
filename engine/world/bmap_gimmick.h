/*
 * engine/world/bmap_gimmick.h -- the models a gym's field gimmick adds to
 * its map (bmap_gimmick.c), for the battle backdrop (docs/battle-backdrop.md).
 */

#ifndef EIDOS_ENGINE_WORLD_BMAP_GIMMICK_H
#define EIDOS_ENGINE_WORLD_BMAP_GIMMICK_H

#ifdef __cplusplus
extern "C" {
#endif

/* One expanded object (FLD_EXP_OBJ) of a gimmick, in its initial state. */
typedef struct BmapGimmickModel {
    const char *arc;    /* archive path of the unit, e.g. "a/1/2/2"              */
    unsigned model;     /* member that holds the BMD0                            */
    int tex;            /* member with a separate BTX0; -1: the BMD0's own       */
    float x, y, z;      /* world position, map units (the game's fx32 / 4096)    */
    float turns;        /* yaw, full turns (0 unless the gimmick turns it)       */
    int anim;           /* member with a BCA0 pose for the node transforms, -1:
                           the model's own pose                                  */
    int frame;          /* frame of that pose, -1: its last frame                */
} BmapGimmickModel;

/* The models to add to `zone`'s scene.  Returns their count (0 for a zone
 * without a gimmick) and sets *out to the first. */
unsigned bmap_gimmick_models(unsigned zone, const BmapGimmickModel **out);

#ifdef __cplusplus
}
#endif

#endif
