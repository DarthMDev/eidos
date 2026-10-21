/*
 * tests/test_mod_avatar.c -- the field avatars: the other player's figure
 * in the local field.
 *
 * The contract is the one of docs/mod-services.md, "Field avatars", and of
 * docs/mod-coop.md, sections 1 to 4: the port makes a mod's partner into a
 * field entity of the game's own entity pool, moves it where the mod says
 * the partner is, and takes it away again at every map change.  The layer
 * is sdk/mod_avatar.c; the services table in sdk/mod_state.h carries the
 * three calls a mod reaches.
 *
 * What is checked here:
 *
 *   - the create: the avatar is a record of the game's own pool, its
 *     object's id is outside the game's range (the zone's people have the
 *     ids of the event data, the player has 0xFF), it carries no event (the
 *     game's own 0xFFFF), the hit checks leave it alone, the game's own
 *     move procedure never moves it, and the handle the port gives the mod
 *     is the port's own number and never a pointer;
 *   - the sprites the game takes from the zone's own event data (162 to
 *     177) are refused, and so is a sprite that is none at all;
 *   - the move: the avatar walks towards the cell the mod named, it does
 *     not jump, the game is told where it is, and the facing follows the
 *     direction the mod gave;
 *   - the frame: the walk advances a sixteenth of a cell a frame, and the
 *     avatar reaches the cell it was sent to;
 *   - the delete: the record is gone from the pool, and a record the game
 *     deleted behind the port's back is not deleted twice;
 *   - a handle the port does not know is ignored, and a handle of an
 *     avatar that is gone never names another one;
 *   - the pool's headroom: the port keeps eight of the sixty-four records
 *     for the zone's own people, which the game adds right after the
 *     route.change event, and a create past the headroom answers 0;
 *   - the port's own table holds sixteen avatars;
 *   - the reset (the map change): every avatar is forgotten and its record
 *     deleted, and the avatars the game has already deleted are only
 *     forgotten.
 *
 * The layer calls the game's own functions weakly, so a build without the
 * game still links and a call without the game reports nothing.  This test
 * stands in for them: a pool of sixty-four records, the accessors the
 * layer reaches the pool through, and the counters that say what the port
 * asked of the game (the record it added, the position and the cell it
 * wrote, the facing, the bits it set, the delete it asked for).
 *
 * What this test does not cover: the game's own drawing and animating of
 * the record (the game does it with its own code), the services table the
 * loader binds the three calls into (sdk/mod_loader.c), and the event row
 * that calls the reset at the map change (sdk/mod_event_field.c).
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "mod_avatar.h"
#include "port.h"
#include "types.h"

#include "port_test.h"

/* ------------------------------------------------------------------ */
/* The test's own names                                                */
/* ------------------------------------------------------------------ */

/* The entity pool's size: the game creates it with 0x40 records
 * (FLDMMDL_CreateSys 0x0216CC58, src/game/arm9/game_data.c). */
#define T_POOL_MAX 64

/* The records the port keeps for the zone's own people, which the game
 * adds right after the route.change event. */
#define T_POOL_RESERVE 8

/* How many avatars the port holds at once. */
#define T_AVATAR_MAX 16

/* The first object's id the port gives its avatars. */
#define T_OBJ_ID_BASE 0x8000u

/* The player's own object id, the one the game gives the player. */
#define T_PLAYER_OBJ_ID 0xFFu

/* The event id of a model with no event: the game's own 0xFFFF. */
#define T_EVENT_ID_NONE 0xFFFFu

/* The zone the player is in. */
#define T_ZONE 7u

/* The sprites: the game's own object codes (1 the hero, 4 the heroine). */
#define T_SPRITE_HERO    1u
#define T_SPRITE_HEROINE 4u

/* The object codes the game takes from the zone's own event data. */
#define T_SPRITE_ZONE_FIRST 162u
#define T_SPRITE_ZONE_LAST  177u

/* The directions: the game's own (0 up, 1 down, 2 left, 3 right). */
#define T_DIR_UP    0u
#define T_DIR_DOWN  1u
#define T_DIR_LEFT  2u
#define T_DIR_RIGHT 3u

/* How the other player moves. */
#define T_MODE_WALKING 0u
#define T_MODE_RUNNING 1u

/* The status bits the port sets on an avatar. */
#define T_STABIT_FELLOW_HIT_OFF 0x0080u
#define T_STABIT_ZONE_DEL_OFF   0x0020u

/* A cell in the game's own fixed point, and a sixteenth of one: the walk
 * the port gives an avatar is a step of that size a frame. */
#define T_CELL      0x10000
#define T_STEP      0x1000

/* ------------------------------------------------------------------ */
/* The game's own types, stood in for                                 */
/* ------------------------------------------------------------------ */

/*
 * The game's own types, named only as the opaque types its accessors
 * take.  The port never looks inside them: every value comes back out
 * through another accessor call.
 */
struct GameData;
struct PlayerWork;
struct MmdlSys;
struct Mmdl;
struct MmdlHeader;

/*
 * The game's own placement record, the 0x24 bytes FLDMMDL_AddMmdl takes
 * (src/game/ov010/fldmmdl_types.h).  The test reads the fields out of the
 * record the port gave the game, so the port's own copy of the layout is
 * checked member by member.
 */
struct MmdlHeader {
    u16 obj_id;        /* 0x00: the object's id */
    u16 obj_code;      /* 0x02: the sprite */
    u16 move_code;     /* 0x04: how the model moves by itself */
    u16 event_type;    /* 0x06: the kind of event the model carries */
    u16 event_flag;    /* 0x08: the flag of that event */
    u16 event_id;      /* 0x0A: the script of that event */
    u16 dir_header;    /* 0x0C: the direction the model faces */
    u16 param0;        /* 0x0E */
    u16 param1;        /* 0x10 */
    u16 param2;        /* 0x12 */
    s16 move_limit_x;  /* 0x14 */
    s16 move_limit_z;  /* 0x16 */
    int pos_type;      /* 0x18: 0 a grid cell, else a rail one */
    union {
        struct {
            u16 grid_x;   /* 0x1C */
            u16 grid_z;   /* 0x1E */
            int pos_y;    /* 0x20 */
        } grid;
        u16 raw[4];
    } pos;
};

/* The record the placement header carries is the game's own size. */
typedef char test_header_is_24[(sizeof(struct MmdlHeader) == 0x24u) ? 1 : -1];

/*
 * The vector the field models' position comes in: the game's own VecFx32,
 * three fx32 (src/game/ov010/fldmmdl_types.h).  The port's own copy of it
 * is the one it hands to the game's accessors, and the two have to be the
 * same three fx32 in the same order.
 */
struct TestVecFx32 {
    fx32 x;
    fx32 y;
    fx32 z;
};

typedef char test_vec_is_12[(sizeof(struct TestVecFx32) == 12u) ? 1 : -1];

/* One record of the pool. */
struct Mmdl {
    int  in_use;          /* the record is in the pool                */
    u16  obj_id;          /* the object's id of the record            */
    u16  obj_code;        /* the sprite of the record                 */
    u16  move_code;       /* how the record moves by itself           */
    u16  event_type;      /* the kind of event the record carries     */
    u16  event_id;        /* the script of that event                 */
    u16  dir_header;      /* the facing the record was made with      */
    u16  zone_id;         /* the zone the record was made in          */
    u16  dir;             /* the facing the game was last told        */
    u16  acmd;            /* the animation the game was last told     */
    u32  status_bit;      /* the status bits of the record            */
    fx32 pos_x;           /* the position, in the game's fixed point  */
    fx32 pos_y;
    fx32 pos_z;
    s16  grid_x;          /* the cell the game was last told          */
    s16  grid_y;
    s16  grid_z;
    int  zone_delete_off; /* the record is kept when the zone changes */
    int  move_paused;     /* the game's own move procedure is off     */
    int  deleted;         /* how many times the record was deleted    */
};

/* The system the pool hangs off. */
struct MmdlSys {
    u16 mmdl_max;         /* how many records the pool holds */
    struct Mmdl *mmdl_buf;
};

/* The game data the port is given; the pool hangs off it. */
struct GameData {
    struct MmdlSys sys;
};

/* The player's own work: the zone the player is in. */
struct PlayerWork {
    u16 zone_id;
};

/* ------------------------------------------------------------------ */
/* The game of the test                                               */
/* ------------------------------------------------------------------ */

/* The pool, the game data and the player the port finds them through. */
static struct Mmdl      g_pool[T_POOL_MAX];
static struct MmdlSys   g_sys;
static struct GameData  g_game_data;
static struct PlayerWork g_player;

/* What the port asked of the game. */
static int g_calls_add;             /* the records the port had made   */
static int g_calls_delete;          /* the deletes the port had asked  */
static int g_calls_delete_gone;     /* a delete of a record not there  */
static int g_calls_set_pos;         /* the positions the port wrote   */
static int g_calls_set_grid;        /* the cells the port wrote       */
static int g_calls_set_dir;         /* the facings the port told      */
static int g_calls_set_status;      /* the status bits the port set   */
static int g_calls_zone_delete_off; /* the zone-delete bit            */
static int g_calls_move_pause;      /* the move pause                 */
static int g_calls_set_acmd;        /* the animations the port set    */
static int g_calls_convert;         /* the codes the port asked for   */
static int g_writes_gone;           /* a write to a record not there  */

/* The last of what the port asked for. */
static struct MmdlHeader g_last_header;
static u16  g_last_zone;
static fx32 g_last_pos_x;
static fx32 g_last_pos_z;
static s16  g_last_grid_x;
static s16  g_last_grid_z;
static u16  g_last_dir;
static u16  g_last_acmd;
static int  g_last_dir_asked;       /* the direction of the animation */
static int  g_last_base_asked;      /* the base of the animation      */

/* A cell in the game's own fixed point: the middle of the cell, the way
 * the game places a model on a grid cell. */
static fx32 cell_fx32(s16 cell)
{
    return (fx32)(((s32)cell << 16) + 0x8000);
}

/* The game data: the pool the field map uses. */
void *GAME_DATA_GetOccupyInfo(const struct GameData *gd)
{
    return (void *)&((struct GameData *)gd)->sys;
}

/* The player's own work. */
struct PlayerWork *GAME_DATA_GetMyPlayerWork(struct GameData *gd)
{
    (void)gd;
    return &g_player;
}

/* The zone the player is in. */
u16 GAME_DATA_GetPlayerZoneId(const struct PlayerWork *player)
{
    return player->zone_id;
}

/* How many records the pool holds. */
u16 FLDMMDL_SysGetMmdlMax(const struct MmdlSys *sys)
{
    return sys->mmdl_max;
}

/* The first free record of the pool, or none at all. */
struct Mmdl *FLDMMDL_SysGetFreeMmdl(struct MmdlSys *sys)
{
    u32 i;

    for (i = 0; i < (u32)sys->mmdl_max; i++) {
        if (!sys->mmdl_buf[i].in_use) {
            return &sys->mmdl_buf[i];
        }
    }
    return NULL;
}

/* The records of the pool that are in use, one at a time. */
int FLDMMDL_SysSearchUseMmdl(const struct MmdlSys *sys, struct Mmdl **out,
                             u32 *iter)
{
    u32 max = (u32)sys->mmdl_max;

    while (*iter < max) {
        struct Mmdl *mmdl = &sys->mmdl_buf[*iter];

        (*iter)++;
        if (mmdl->in_use) {
            *out = mmdl;
            return 1;
        }
    }
    return 0;
}

/* The record with that object's id, or none at all. */
struct Mmdl *FLDMMDL_SysSearchObjId(struct MmdlSys *sys, u32 obj_id)
{
    u32 i;

    for (i = 0; i < (u32)sys->mmdl_max; i++) {
        struct Mmdl *mmdl = &sys->mmdl_buf[i];

        if (mmdl->in_use && (u32)mmdl->obj_id == obj_id) {
            return mmdl;
        }
    }
    return NULL;
}

/*
 * A record is made the way the game makes one: the header's own fields are
 * copied into the record, and the record is placed on the cell the header
 * names.
 */
struct Mmdl *FLDMMDL_AddMmdl(struct MmdlSys *sys,
                             const struct MmdlHeader *header, int zone_id)
{
    struct Mmdl *mmdl = FLDMMDL_SysGetFreeMmdl(sys);

    g_calls_add++;
    if (mmdl == NULL) {
        return NULL;   /* the pool is full */
    }
    memset(mmdl, 0, sizeof(*mmdl));
    mmdl->in_use = 1;
    mmdl->obj_id = header->obj_id;
    mmdl->obj_code = header->obj_code;
    mmdl->move_code = header->move_code;
    mmdl->event_type = header->event_type;
    mmdl->event_id = header->event_id;
    mmdl->dir_header = header->dir_header;
    mmdl->zone_id = (u16)zone_id;
    mmdl->status_bit = 0x0001u;    /* the game's own "in use" bit */
    mmdl->grid_x = (s16)header->pos.grid.grid_x;
    mmdl->grid_z = (s16)header->pos.grid.grid_z;
    mmdl->pos_x = cell_fx32(mmdl->grid_x);
    mmdl->pos_z = cell_fx32(mmdl->grid_z);
    g_last_header = *header;
    g_last_zone = (u16)zone_id;
    return mmdl;
}

/* A record goes away. */
void FLDMMDL_DeleteMmdl(struct Mmdl *mmdl)
{
    g_calls_delete++;
    if (!mmdl->in_use) {
        g_calls_delete_gone++;   /* the record was already gone */
        return;
    }
    mmdl->in_use = 0;
    mmdl->deleted++;
}

/* Where the record is. */
void FLDMMDL_GetPos(const struct Mmdl *mmdl, struct TestVecFx32 *out)
{
    out->x = mmdl->pos_x;
    out->y = mmdl->pos_y;
    out->z = mmdl->pos_z;
}

/* The record is put somewhere else. */
void FLDMMDL_SetPos(struct Mmdl *mmdl, const struct TestVecFx32 *pos)
{
    g_calls_set_pos++;
    if (!mmdl->in_use) {
        g_writes_gone++;
        return;
    }
    mmdl->pos_x = pos->x;
    mmdl->pos_y = pos->y;
    mmdl->pos_z = pos->z;
    g_last_pos_x = pos->x;
    g_last_pos_z = pos->z;
}

/* The cell the record is in. */
void FLDMMDL_SetGridNowX(struct Mmdl *mmdl, short x)
{
    g_calls_set_grid++;
    if (!mmdl->in_use) {
        g_writes_gone++;
        return;
    }
    mmdl->grid_x = x;
    g_last_grid_x = x;
}

void FLDMMDL_SetGridNowY(struct Mmdl *mmdl, int y)
{
    g_calls_set_grid++;
    if (!mmdl->in_use) {
        g_writes_gone++;
        return;
    }
    mmdl->grid_y = (s16)y;
}

void FLDMMDL_SetGridNowZ(struct Mmdl *mmdl, int z)
{
    g_calls_set_grid++;
    if (!mmdl->in_use) {
        g_writes_gone++;
        return;
    }
    mmdl->grid_z = (s16)z;
    g_last_grid_z = (s16)z;
}

/* The facing the record shows. */
void FLDMMDL_SetDirAll(struct Mmdl *mmdl, u16 dir)
{
    g_calls_set_dir++;
    if (!mmdl->in_use) {
        g_writes_gone++;
        return;
    }
    mmdl->dir = dir;
    g_last_dir = dir;
}

/* The status bits of the record. */
void FLDMMDL_SetStatusBit(struct Mmdl *mmdl, u32 bit)
{
    g_calls_set_status++;
    mmdl->status_bit |= bit;
}

/* The record is kept when the zone changes inside the map. */
void FLDMMDL_SetStatusBitZoneDeleteOff(struct Mmdl *mmdl, int flag)
{
    g_calls_zone_delete_off++;
    mmdl->zone_delete_off = flag;
}

/* The game's own move procedure never moves the record. */
void FLDMMDL_OnMoveBitMovePause(struct Mmdl *mmdl)
{
    g_calls_move_pause++;
    mmdl->move_paused = 1;
}

/* The animation of the record. */
void FLDMMDL_SetAcmd(struct Mmdl *mmdl, u16 code)
{
    g_calls_set_acmd++;
    if (!mmdl->in_use) {
        g_writes_gone++;
        return;
    }
    mmdl->acmd = code;
    g_last_acmd = code;
}

/* The fake's animation has ended, so the port may set the next one. */
int FLDMMDL_CheckEndAcmd(struct Mmdl *mmdl)
{
    (void)mmdl;
    return 1;
}

/* The game's own code for a direction: the fake's is the base plus the
 * direction, so the code the port sets says which direction it asked for. */
u16 FLDMMDL_ACMD_ConvertCodeByDir(int index, int code)
{
    g_calls_convert++;
    g_last_dir_asked = index;
    g_last_base_asked = code;
    return (u16)(code + index);
}

/* ------------------------------------------------------------------ */
/* The helpers                                                         */
/* ------------------------------------------------------------------ */

/* How many records of the pool are in use. */
static int pool_used(void)
{
    struct Mmdl *mmdl = NULL;
    u32 iter = 0;
    int used = 0;

    while (FLDMMDL_SysSearchUseMmdl(&g_sys, &mmdl, &iter) == 1) {
        used++;
    }
    return used;
}

/*
 * A person of the zone, added the way the game adds them: the game fills a
 * free record of the pool itself.  The test does it by hand, so the
 * counters say only what the port asked of the game.
 */
static struct Mmdl *add_zone_person(u16 obj_id)
{
    struct Mmdl *mmdl = FLDMMDL_SysGetFreeMmdl(&g_sys);

    if (mmdl == NULL) {
        return NULL;
    }
    memset(mmdl, 0, sizeof(*mmdl));
    mmdl->in_use = 1;
    mmdl->obj_id = obj_id;
    mmdl->obj_code = 5u;
    mmdl->event_id = 0x0010u;   /* a script of the zone */
    mmdl->status_bit = 0x0001u;
    return mmdl;
}

/* The game empties the pool at a map change: the record goes without the
 * port's asking. */
static void game_delete(struct Mmdl *mmdl)
{
    mmdl->in_use = 0;
    mmdl->deleted++;
}

/* The pool the fake holds, emptied, and the port's own table with it. */
static void reset_pool(void)
{
    bw_mod_avatar_reset();
    memset(g_pool, 0, sizeof g_pool);
    g_calls_add = 0;
    g_calls_delete = 0;
    g_calls_delete_gone = 0;
    g_calls_set_pos = 0;
    g_calls_set_grid = 0;
    g_calls_set_dir = 0;
    g_calls_set_status = 0;
    g_calls_zone_delete_off = 0;
    g_calls_move_pause = 0;
    g_calls_set_acmd = 0;
    g_calls_convert = 0;
    g_writes_gone = 0;
}

/* The record of an avatar, found the way the port finds it. */
static struct Mmdl *record_of(u32 handle)
{
    u32 obj_id = bw_mod_avatar_obj_id(handle);

    if (obj_id == 0u) {
        return NULL;
    }
    return FLDMMDL_SysSearchObjId(&g_sys, obj_id);
}

/* ------------------------------------------------------------------ */
/* The test                                                            */
/* ------------------------------------------------------------------ */

int main(void)
{
    u32 handle;
    u32 handle2;
    u32 obj_id;
    s16 x, y, z;
    struct Mmdl *rec;
    int i;

    printf("the seam\n");
    BW_CHECK_EQ("without the game there is no pool",
                (long)bw_mod_avatar_pool_max(), 0);
    BW_CHECK_EQ("and no room in it",
                (long)bw_mod_avatar_pool_free(), 0);
    BW_CHECK_EQ("and no avatar can be made",
                (long)bw_mod_avatar_create(T_SPRITE_HERO, 10, 12, T_DIR_DOWN),
                0);
    BW_CHECK_EQ("and the port holds none",
                (long)bw_mod_avatar_count(), 0);

    memset(g_pool, 0, sizeof g_pool);
    g_sys.mmdl_max = T_POOL_MAX;
    g_sys.mmdl_buf = g_pool;
    g_game_data.sys = g_sys;
    g_player.zone_id = T_ZONE;
    bw_mod_avatar_set_game_data(&g_game_data);

    BW_CHECK_EQ("the pool is the game's own",
                (long)bw_mod_avatar_pool_max(), (long)T_POOL_MAX);
    BW_CHECK_EQ("and it is empty",
                (long)bw_mod_avatar_pool_free(), (long)T_POOL_MAX);

    printf("the create\n");
    handle = bw_mod_avatar_create(T_SPRITE_HERO, 10, 12, T_DIR_DOWN);
    BW_REQUIRE("an avatar is made", handle != 0u);
    BW_CHECK_EQ("the port holds one",
                (long)bw_mod_avatar_count(), 1);
    BW_CHECK_EQ("the game was asked for one record",
                (long)g_calls_add, 1);
    BW_CHECK_EQ("the record is in the pool",
                (long)pool_used(), 1);
    BW_CHECK_EQ("and the pool's free room is one smaller",
                (long)bw_mod_avatar_pool_free(), (long)(T_POOL_MAX - 1));

    /* The handle is the port's own number, never a pointer. */
    BW_CHECK_EQ("the handle is a number, not a pointer",
                (long)(handle < 0x1000000u), 1);
    rec = record_of(handle);
    BW_REQUIRE("the record is in the pool", rec != NULL);
    BW_CHECK_EQ("and it is not the record's address",
                (long)((uintptr_t)handle != (uintptr_t)rec), 1);

    /* The object's id is outside the game's range. */
    obj_id = bw_mod_avatar_obj_id(handle);
    BW_CHECK_EQ("the object's id is the port's own",
                (long)(obj_id >= T_OBJ_ID_BASE), 1);
    BW_CHECK_EQ("and it is not the player's own id",
                (long)(obj_id == T_PLAYER_OBJ_ID), 0);
    BW_CHECK_EQ("and it is not one of the zone's people's ids",
                (long)(obj_id < 0x100u), 0);

    /* The record the port gave the game. */
    BW_CHECK_EQ("the record carries the port's own object id",
                (long)g_last_header.obj_id, (long)obj_id);
    BW_CHECK_EQ("the sprite is the game's own object code",
                (long)g_last_header.obj_code, (long)T_SPRITE_HERO);
    BW_CHECK_EQ("the model does not move by itself",
                (long)g_last_header.move_code, 0);
    BW_CHECK_EQ("the record carries no event",
                (long)g_last_header.event_id, (long)T_EVENT_ID_NONE);
    BW_CHECK_EQ("and no kind of event either",
                (long)g_last_header.event_type, 0);
    BW_CHECK_EQ("the facing is the one the mod gave",
                (long)g_last_header.dir_header, (long)T_DIR_DOWN);
    BW_CHECK_EQ("the record is placed on a grid cell",
                (long)g_last_header.pos_type, 0);
    BW_CHECK_EQ("the cell is the one the mod gave",
                (long)g_last_header.pos.grid.grid_x, 10);
    BW_CHECK_EQ("and its other one",
                (long)g_last_header.pos.grid.grid_z, 12);
    BW_CHECK_EQ("and the record is in the zone the player is in",
                (long)g_last_zone, (long)T_ZONE);

    /* The rules the port keeps on the record. */
    BW_CHECK_EQ("the hit checks leave the avatar alone",
                (long)((rec->status_bit & T_STABIT_FELLOW_HIT_OFF) != 0u), 1);
    BW_CHECK_EQ("the avatar is kept when the zone changes",
                (long)rec->zone_delete_off, 1);
    BW_CHECK_EQ("the game's own move procedure never moves it",
                (long)rec->move_paused, 1);
    BW_CHECK_EQ("the port told the game to pause the move",
                (long)g_calls_move_pause, 1);

    /* Where the avatar is. */
    BW_CHECK_EQ("the avatar stands where it was put",
                (long)rec->pos_x, (long)cell_fx32(10));
    BW_CHECK_EQ("and its other way",
                (long)rec->pos_z, (long)cell_fx32(12));
    BW_CHECK_EQ("the game was told where it is",
                (long)g_last_grid_x, 10);
    BW_CHECK_EQ("and where the other way is",
                (long)g_last_grid_z, 12);
    BW_CHECK_EQ("the port knows where the avatar is",
                (long)bw_mod_avatar_pos(handle, &x, &y, &z), 0);
    BW_CHECK_EQ("and it is the cell the mod gave",
                (long)x, 10);
    BW_CHECK_EQ("and its other one",
                (long)z, 12);
    BW_CHECK_EQ("the port knows the facing",
                (long)bw_mod_avatar_dir(handle), (long)T_DIR_DOWN);
    BW_CHECK_EQ("and the game was told it",
                (long)g_last_dir, (long)T_DIR_DOWN);

    printf("the sprites the game takes from the zone's own data\n");
    BW_CHECK_EQ("the first of them is refused",
                (long)bw_mod_avatar_create(T_SPRITE_ZONE_FIRST, 10, 12,
                                           T_DIR_DOWN), 0);
    BW_CHECK_EQ("and the last of them",
                (long)bw_mod_avatar_create(T_SPRITE_ZONE_LAST, 10, 12,
                                           T_DIR_DOWN), 0);
    BW_CHECK_EQ("and a sprite that is none at all",
                (long)bw_mod_avatar_create(0, 10, 12, T_DIR_DOWN), 0);
    BW_CHECK_EQ("the port holds one avatar all the same",
                (long)bw_mod_avatar_count(), 1);
    BW_CHECK_EQ("and the game was asked for no record",
                (long)g_calls_add, 1);

    printf("the move\n");
    bw_mod_avatar_move(handle, 14, 0, 13, T_DIR_RIGHT, T_MODE_WALKING);
    BW_CHECK_EQ("the avatar does not jump to the cell",
                (long)rec->pos_x, (long)cell_fx32(10));
    BW_CHECK_EQ("nor to the other one",
                (long)rec->pos_z, (long)cell_fx32(12));
    BW_CHECK_EQ("the port holds the facing the mod gave",
                (long)bw_mod_avatar_dir(handle), (long)T_DIR_RIGHT);
    BW_CHECK_EQ("but the game is not told of it until the frame",
                (long)g_last_dir, (long)T_DIR_DOWN);

    printf("the frame\n");
    bw_mod_avatar_frame();
    BW_CHECK_EQ("the walk advances a sixteenth of a cell",
                (long)rec->pos_x, (long)(cell_fx32(10) + T_STEP));
    BW_CHECK_EQ("and the other way too",
                (long)rec->pos_z, (long)(cell_fx32(12) + T_STEP));
    BW_CHECK_EQ("the game is told where the avatar is",
                (long)g_last_grid_x, 10);
    BW_CHECK_EQ("and the facing follows the direction the mod gave",
                (long)rec->dir, (long)T_DIR_RIGHT);
    BW_CHECK_EQ("and the avatar has not arrived yet",
                (long)bw_mod_avatar_pos(handle, &x, &y, &z), 0);
    BW_CHECK_EQ("it is still in the cell it was in",
                (long)x, 10);

    /* Four cells are sixty-four frames of a sixteenth of a cell. */
    for (i = 1; i < 64; i++) {
        bw_mod_avatar_frame();
    }
    BW_REQUIRE("the port knows where the avatar is",
               bw_mod_avatar_pos(handle, &x, &y, &z) == 0);
    BW_CHECK_EQ("the avatar has reached the cell the mod named",
                (long)x, 14);
    BW_CHECK_EQ("and the other one",
                (long)z, 13);
    BW_CHECK_EQ("the game is told where it is",
                (long)g_last_grid_x, 14);
    BW_CHECK_EQ("and where the other way is",
                (long)g_last_grid_z, 13);
    BW_CHECK_EQ("and the position is the cell's own",
                (long)rec->pos_x, (long)cell_fx32(14));
    BW_CHECK_EQ("in the other way too",
                (long)rec->pos_z, (long)cell_fx32(13));

    printf("a handle the port does not know\n");
    bw_mod_avatar_move(0u, 1, 2, 3, T_DIR_LEFT, T_MODE_WALKING);
    bw_mod_avatar_move(handle + 0x010000u, 1, 2, 3, T_DIR_LEFT,
                       T_MODE_WALKING);
    BW_CHECK_EQ("the avatar did not move",
                (long)rec->pos_x, (long)cell_fx32(14));
    BW_CHECK_EQ("and it did not turn",
                (long)bw_mod_avatar_dir(handle), (long)T_DIR_RIGHT);
    bw_mod_avatar_delete(0u);
    bw_mod_avatar_delete(handle + 0x010000u);
    BW_CHECK_EQ("the avatar is still there",
                (long)bw_mod_avatar_count(), 1);
    BW_CHECK_EQ("and the game was asked for no delete",
                (long)g_calls_delete, 0);

    printf("the delete\n");
    bw_mod_avatar_delete(handle);
    BW_CHECK_EQ("the port holds none",
                (long)bw_mod_avatar_count(), 0);
    BW_CHECK_EQ("the game was asked for one delete",
                (long)g_calls_delete, 1);
    BW_CHECK_EQ("and the record is gone from the pool",
                (long)pool_used(), 0);
    BW_CHECK_EQ("the handle is not known any more",
                (long)bw_mod_avatar_obj_id(handle), 0);
    BW_CHECK_EQ("and the port cannot say where the avatar was",
                (long)bw_mod_avatar_pos(handle, &x, &y, &z), -1);
    BW_CHECK_EQ("nor which way it faced",
                (long)bw_mod_avatar_dir(handle), 0);

    printf("a record the game deleted behind the port's back\n");
    handle = bw_mod_avatar_create(T_SPRITE_HEROINE, 20, 22, T_DIR_UP);
    BW_REQUIRE("an avatar is made", handle != 0u);
    rec = record_of(handle);
    BW_REQUIRE("the record is in the pool", rec != NULL);
    game_delete(rec);
    BW_CHECK_EQ("the record is gone from the pool",
                (long)pool_used(), 0);
    bw_mod_avatar_delete(handle);
    BW_CHECK_EQ("the game was not asked to delete it again",
                (long)g_calls_delete, 1);
    BW_CHECK_EQ("and the port forgot the avatar",
                (long)bw_mod_avatar_count(), 0);

    printf("a record the game deleted, seen by the frame\n");
    handle = bw_mod_avatar_create(T_SPRITE_HEROINE, 20, 22, T_DIR_UP);
    BW_REQUIRE("an avatar is made", handle != 0u);
    rec = record_of(handle);
    BW_REQUIRE("the record is in the pool", rec != NULL);
    game_delete(rec);
    g_writes_gone = 0;
    bw_mod_avatar_frame();
    BW_CHECK_EQ("the port wrote to no record that is not there",
                (long)g_writes_gone, 0);
    BW_CHECK_EQ("and the avatar is gone",
                (long)bw_mod_avatar_count(), 0);
    BW_CHECK_EQ("and the game was asked for no delete",
                (long)g_calls_delete, 1);

    printf("the handle\n");
    reset_pool();
    handle = bw_mod_avatar_create(T_SPRITE_HERO, 10, 12, T_DIR_DOWN);
    BW_REQUIRE("an avatar is made", handle != 0u);
    bw_mod_avatar_delete(handle);
    handle2 = bw_mod_avatar_create(T_SPRITE_HERO, 10, 12, T_DIR_DOWN);
    BW_REQUIRE("and another", handle2 != 0u);
    BW_CHECK_EQ("the new avatar has a handle of its own",
                (long)(handle2 != handle), 1);
    BW_CHECK_EQ("the old handle names nothing",
                (long)bw_mod_avatar_obj_id(handle), 0);
    BW_CHECK_EQ("while the new one names the new avatar",
                (long)(bw_mod_avatar_obj_id(handle2) != 0u), 1);

    printf("the pool's headroom\n");
    reset_pool();
    /* The zone's people: the game adds them right after the route.change
     * event, where the mod makes the avatars of the new zone. */
    for (i = 0; i < T_POOL_MAX - T_POOL_RESERVE - 1; i++) {
        BW_REQUIRE("the zone's people fit",
                   add_zone_person((u16)(i + 1)) != NULL);
    }
    BW_CHECK_EQ("the pool has nine free records",
                (long)bw_mod_avatar_pool_free(), (long)(T_POOL_RESERVE + 1));
    handle = bw_mod_avatar_create(T_SPRITE_HERO, 30, 30, T_DIR_DOWN);
    BW_REQUIRE("an avatar is made", handle != 0u);
    BW_CHECK_EQ("the port keeps eight records for the zone's own people",
                (long)bw_mod_avatar_pool_free(), (long)T_POOL_RESERVE);
    BW_CHECK_EQ("and a create past the headroom is refused",
                (long)bw_mod_avatar_create(T_SPRITE_HERO, 30, 30,
                                           T_DIR_DOWN), 0);
    BW_CHECK_EQ("and the refusal takes no record",
                (long)pool_used(), (long)(T_POOL_MAX - T_POOL_RESERVE));
    BW_CHECK_EQ("and the pool's free room is the same",
                (long)bw_mod_avatar_pool_free(), (long)T_POOL_RESERVE);

    printf("the port's own table\n");
    reset_pool();
    for (i = 0; i < T_AVATAR_MAX; i++) {
        handle = bw_mod_avatar_create(T_SPRITE_HERO, 30, 30, T_DIR_DOWN);
        BW_REQUIRE("an avatar is made", handle != 0u);
    }
    BW_CHECK_EQ("the port holds sixteen",
                (long)bw_mod_avatar_count(), (long)T_AVATAR_MAX);
    BW_CHECK_EQ("and the pool has room for more",
                (long)bw_mod_avatar_pool_free(),
                (long)(T_POOL_MAX - T_AVATAR_MAX));
    BW_CHECK_EQ("but the table is full",
                (long)bw_mod_avatar_create(T_SPRITE_HERO, 30, 30,
                                           T_DIR_DOWN), 0);
    BW_CHECK_EQ("and the refusal takes no record",
                (long)pool_used(), (long)T_AVATAR_MAX);
    BW_CHECK_EQ("and the pool's free room is the same",
                (long)bw_mod_avatar_pool_free(),
                (long)(T_POOL_MAX - T_AVATAR_MAX));

    printf("the reset\n");
    reset_pool();
    handle = bw_mod_avatar_create(T_SPRITE_HERO, 40, 42, T_DIR_DOWN);
    BW_REQUIRE("an avatar is made", handle != 0u);
    handle2 = bw_mod_avatar_create(T_SPRITE_HEROINE, 41, 42, T_DIR_DOWN);
    BW_REQUIRE("and another", handle2 != 0u);
    BW_CHECK_EQ("the port holds two",
                (long)bw_mod_avatar_count(), 2);
    bw_mod_avatar_reset();
    BW_CHECK_EQ("every avatar is forgotten",
                (long)bw_mod_avatar_count(), 0);
    BW_CHECK_EQ("and its record is deleted",
                (long)g_calls_delete, 2);
    BW_CHECK_EQ("and the pool is empty again",
                (long)pool_used(), 0);
    BW_CHECK_EQ("the first handle is not known any more",
                (long)bw_mod_avatar_obj_id(handle), 0);
    BW_CHECK_EQ("and the second is not either",
                (long)bw_mod_avatar_obj_id(handle2), 0);

    printf("the reset after the game emptied the pool\n");
    reset_pool();
    handle = bw_mod_avatar_create(T_SPRITE_HERO, 40, 42, T_DIR_DOWN);
    BW_REQUIRE("an avatar is made", handle != 0u);
    rec = record_of(handle);
    BW_REQUIRE("the record is in the pool", rec != NULL);
    game_delete(rec);
    bw_mod_avatar_reset();
    BW_CHECK_EQ("the avatar is forgotten",
                (long)bw_mod_avatar_count(), 0);
    BW_CHECK_EQ("and the game is not asked to delete it again",
                (long)g_calls_delete, 0);

    printf("the port's own reset\n");
    reset_pool();
    handle = bw_mod_avatar_create(T_SPRITE_HERO, 40, 42, T_DIR_DOWN);
    BW_REQUIRE("an avatar is made", handle != 0u);
    bw_port_reset();
    BW_CHECK_EQ("the reset forgets the avatar",
                (long)bw_mod_avatar_count(), 0);
    BW_CHECK_EQ("and its record is deleted",
                (long)g_calls_delete, 1);
    BW_CHECK_EQ("and the pool is empty again",
                (long)pool_used(), 0);

    BW_REPORT("test_mod_avatar");
}
