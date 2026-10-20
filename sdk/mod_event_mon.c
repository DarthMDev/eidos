/*
 * sdk/mod_event_mon.c -- the pokemon area's event rows: the Pokemon that is
 * created, received, nicknamed, evolved, given experience, levelled up, that
 * learns a move, that is released, that moves between the party and a box,
 * that swaps its place in the party, that is given a held item, that hatches
 * and that is traded (docs/mod-events.md, section 1, "Pokemon";
 * docs/mod-implementation.md, section 1, "The binding table"; the payloads
 * are docs/mod-payloads.md, section 2).
 *
 * One row per stable-event binding of this area.  A row names the game
 * function the event fires from (the region and the address of
 * mod/declared_set.txt), the event, the point, the scope, the builder that
 * fills the payload from the call, the applier that writes the modifiable
 * fields back and the refusal function that produces the refused outcome.
 * The dispatch core reads the rows from the table this file publishes
 * (bw_event_bindings_mon) and registers them with bw_mod_event_bind_area.
 *
 * The rules the port keeps (docs/mod-payloads.md, section 0): the port owns
 * the payload; a field is filled from the call's arguments, from an accessor
 * call at the slot or from a context; a field the call cannot supply stays
 * 0 and says so in a comment; a field that points at memory the game frees
 * later is copied into the payload, never stored as a pointer.
 *
 * Two things are particular to this area.
 *
 * The builders run in the ARM9, where an overlay's own code is not
 * reachable: a builder calls the ARM9's accessors, and it reads an
 * overlay's blocks through the port's own views of them (the way
 * sdk/mod_event_items.c reads the shop's work).  Every view below is copied
 * member for member from the game's own header, so the offsets are the
 * game's at either pointer width.
 *
 * The area binds the game's live objects where it learns them: the game
 * data at the rows that carry it (the ones that give the player a Pokemon)
 * and the battle's main object at the row that works out the battle's
 * experience shares, so that the port's state calls (sdk/mod_state.h)
 * answer from then on.  The field player is not one of this area's rows:
 * the field screen's own rows are where the port learns it.
 *
 * The game's own accessors are declared under the names src/game gives
 * them, the way sdk/mod_state.c does: the port's older address spellings
 * are redirected onto the same names by the build's gamespell header
 * (tools/gamespell.py), so one spelling links against either tree.
 */
#include "mod_event.h"
#include "mod_slot.h"
#include "mod_state.h"

#include <stddef.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* The game's own types                                                */
/* ------------------------------------------------------------------ */

/*
 * The game's structures, named only as the opaque types its accessors
 * take.  The port never looks inside them: every value comes back out
 * through another accessor call.  The tags are the game's own
 * (src/game/arm9/game_data.h and its siblings), so each prototype below
 * can be matched against the corpus by name.
 */
struct GameData;
struct GameSystem;
struct Party;
struct PokemonParam;
struct BoxPokemon;
struct EventWork;

/* ------------------------------------------------------------------ */
/* The game's own accessors                                            */
/* ------------------------------------------------------------------ */

/*
 * The game's own functions.  They are weak so that a link without the game
 * still succeeds; the port's own seam, sdk/mod_state.c, does the same.
 */
#if defined(__GNUC__) || defined(__clang__)
#define BW_PORT_WEAK __attribute__((weak))
#else
#define BW_PORT_WEAK
#endif

/* The game data and the party of the running game. */
extern void *GAME_DATA_GetMyParty(const struct GameData *gameData)
    BW_PORT_WEAK;                                        /* 0x02012934 */
extern u8 *GAME_DATA_GetEventWork(const struct GameData *gameData)
    BW_PORT_WEAK;                                        /* 0x0201296C */
extern struct GameData *GAMESYSTEM_GetGameData(const struct GameSystem *gameSystem)
    BW_PORT_WEAK;                                        /* 0x020120F4 */

/* The party. */
extern u32 POKEPARTY_GetCount(const struct Party *party)
    BW_PORT_WEAK;                                        /* 0x0201AA18 */
extern struct PokemonParam *POKEPARTY_GetMember(struct Party *party, u32 pos)
    BW_PORT_WEAK;                                        /* 0x0201AB28 */

/* A Pokemon's own fields (src/game/arm9/poke_tool_param.h). */
extern u32 POKE_TOOL_GetPartyValue(struct PokemonParam *mon, u32 paramId, u32 arg)
    BW_PORT_WEAK;                                        /* 0x02017E1C */
extern u32 POKE_TOOL_GetBoxValue(struct BoxPokemon *box, u32 paramId, u32 arg)
    BW_PORT_WEAK;                                        /* 0x02017E88 */
extern u32 POKE_TOOL_IsShiny(struct PokemonParam *mon)
    BW_PORT_WEAK;                                        /* 0x02017EAC */
extern u32 POKE_TOOL_IsShinyOtIdPersonality(u32 otId, u32 personality)
    BW_PORT_WEAK;                                        /* 0x02017ED8 */

/* The species' level-up table (src/game/arm9/poke_personal.c). */
extern void POKE_PERSONAL_LoadDataDirect(u32 species, int form, void *dst)
    BW_PORT_WEAK;                                        /* 0x0201ADD0 */

/* The boxes. */
extern struct BoxPokemon *BOX_SAVEDATA_GetPokemon(struct GameData *gameData,
                                                   u32 box, u32 slot)
    BW_PORT_WEAK;                                        /* 0x02007A18 */

/* The event work: the game's shared script variables. */
extern u16 *ZONEDATA_GetEventWorkAddress(struct EventWork *ev, u32 workId)
    BW_PORT_WEAK;                                        /* 0x020143CC */

/* ------------------------------------------------------------------ */
/* The Pokemon record's own field ids                                 */
/* ------------------------------------------------------------------ */

/*
 * The ids the game's own accessors take (src/game/arm9/poke_tool_param.h),
 * the same ids the port's state table names (sdk/mod_state.h).
 */
#define BW_PORT_FIELD_PERSONALITY 0x00u
#define BW_PORT_FIELD_SPECIES     0x05u
#define BW_PORT_FIELD_ITEM        0x06u
#define BW_PORT_FIELD_EXP         0x08u
#define BW_PORT_FIELD_MOVE1       0x36u
#define BW_PORT_FIELD_IS_EGG      0x4Cu
#define BW_PORT_FIELD_FORM        0x6Fu
#define BW_PORT_FIELD_LEVEL       0x9Eu

/* ------------------------------------------------------------------ */
/* The port's own views of the game's work blocks                     */
/* ------------------------------------------------------------------ */

/*
 * The port's own check, in the shape sdk/os_thread.c uses: a condition that
 * must hold at every pointer width the port is built for.  The name is the
 * port's own, so a header that later grows one does not clash with it.
 */
#define BW_PORT_STATIC_ASSERT(name, cond) \
    typedef char bw_port_static_assert_##name[(cond) ? 1 : -1]

/*
 * The script command's own work, copied member for member from the game's
 * own header (aletheia src/game/ov010/scrcmd_work.c).  The script commands
 * of this area take it as their second argument: the game data and the
 * script work live in its common block, and the script's own variables are
 * in the script work's local work.
 */
typedef struct BwPortScrcmdCommon {
    void *scriptWork;   /* 0x00 */
    void *gameSystem;   /* 0x04 */
    void *gameData;     /* 0x08 */
} BwPortScrcmdCommon;

typedef struct BwPortScrcmdParam {
    u16   scriptId;    /* 0x00 */
    u16   pad02;       /* 0x02 */
    u32   callParam;   /* 0x04 */
    u32   callMode;    /* 0x08 */
    void *scriptWork;  /* 0x0C */
} BwPortScrcmdParam;

typedef struct BwPortScrcmdWork {
    u16                heapId;     /* 0x00 */
    u16                pad02;      /* 0x02 */
    BwPortScrcmdParam  param;      /* 0x04 */
    void              *msgData;    /* 0x14 */
    u16                msgDataId;  /* 0x18 */
    u8                 childScriptIndex; /* 0x1A */
    u8                 pad1B;      /* 0x1B */
    void              *savedLocalWork; /* 0x1C */
    BwPortScrcmdCommon *common;     /* 0x20 */
} BwPortScrcmdWork;

BW_PORT_STATIC_ASSERT(scrcmd_common, sizeof(void *) != 4 ||
                      offsetof(BwPortScrcmdWork, common) == 0x20u);

/*
 * The script work's own head, copied from the game's header (aletheia
 * src/game/ov010/script_work_types.h).  The script's variables are its
 * local work: 96 halfwords at +0x7C, which is where the game's own
 * SCRIPT_WORK_GetLocalWork (0x02158FD0) reads them from.
 *
 * The copy keeps the game's own member types, so the offsets are the
 * game's at either pointer width: the pointers between the head and the
 * local work are the game's own, not padding.
 */
typedef struct BwPortScriptFieldParam {
    void *mapSystem;   /* 0x00 */
    void *fieldSystem; /* 0x04 */
} BwPortScriptFieldParam;

typedef struct BwPortScriptTrainerEntry {
    u32 params[7];    /* 0x00 */
} BwPortScriptTrainerEntry;

typedef struct BwPortScriptWork {
    u32   magic;                       /* 0x00 */
    u16   scriptId;                    /* 0x04 */
    u16   pad06;                       /* 0x06 */
    void *targetObj;                   /* 0x08 */
    u16   heapId;                      /* 0x0C */
    u16   pad0E;                       /* 0x0E */
    void *gameSystem;                  /* 0x10 */
    void *event;                       /* 0x14 */
    BwPortScriptFieldParam fieldParam; /* 0x18 */
    int   targetObjParam;              /* 0x20 */
    int   isChild;                     /* 0x24 */
    void *parentWork;                  /* 0x28 */
    void *wordSet;                     /* 0x2C */
    void *msgBuffer;                   /* 0x30 */
    void *tempBuffer;                  /* 0x34 */
    void *msgData;                     /* 0x38 */
    void *userWork;                    /* 0x3C */
    u32   trainerParam;                /* 0x40 */
    BwPortScriptTrainerEntry trainers[2]; /* 0x44 */
    u16   localWorks[96];              /* 0x7C: the work ids 0x8000 and up */
} BwPortScriptWork;

BW_PORT_STATIC_ASSERT(script_local_work, sizeof(void *) != 4 ||
                      offsetof(BwPortScriptWork, localWorks) == 0x7Cu);

/*
 * The script VM's own head, copied from the game's header (aletheia
 * src/game/arm9/script_vm_types.h).  The program counter is at +0x14: the
 * command's operands follow it, one halfword each.
 */
typedef struct BwPortScriptVmParams {
    u16   stackCap;  /* 0x00 */
    u16   ringCap;   /* 0x02 */
    void *cmds;      /* 0x04 */
    u32   cmdCount;  /* 0x08 */
} BwPortScriptVmParams;

typedef struct BwPortScriptVm {
    BwPortScriptVmParams params; /* 0x00 */
    u8  depth;      /* 0x0C */
    u8  state;      /* 0x0D */
    u8  cmpFlag;    /* 0x0E */
    u8  pad0F;      /* 0x0F */
    void *wait;     /* 0x10 */
    u8 *pc;         /* 0x14 */
} BwPortScriptVm;

BW_PORT_STATIC_ASSERT(script_vm_pc, sizeof(void *) != 4 ||
                      offsetof(BwPortScriptVm, pc) == 0x14u);

/*
 * The creation request the field script commands of this area share,
 * copied from the game's header (aletheia src/game/ov010/scrcmd_proc.c).
 * It has no pointers, so its offsets are the same at either width.
 */
typedef struct BwPortPokemonSpec {
    u16 heapId;         /* 0x00 */
    u16 pad02;          /* 0x02 */
    int species;        /* 0x04 */
    int form;           /* 0x08 */
    int level;          /* 0x0C */
    int heldItem;       /* 0x10 */
    int genderMode;     /* 0x14 */
    int natureMode;     /* 0x18 */
    int shinyMode;      /* 0x1C */
    int ballItem;       /* 0x20 */
} BwPortPokemonSpec;

/*
 * The battle screen's own arguments and work, copied from the game's
 * header (aletheia src/game/ov092/event_battle_return.c).  The caught
 * Pokemon and the box it went to are in the work.
 */
typedef struct BwPortCatchArgs {
    u32   wasAlreadyCaught; /* 0x00 */
    void *mon;              /* 0x04 */
    u32   dexMode;          /* 0x08 */
    void *boxFullMsg;       /* 0x0C */
    void *boxManager;       /* 0x10 */
    u32   boxNo;            /* 0x14 */
    void *gameData;         /* 0x18 */
    u32   wantsNickname;    /* 0x1C */
} BwPortCatchArgs;

typedef struct BwPortBattleReturnArgs {
    void *gameData;    /* 0x00 */
    void *battleParam; /* 0x04 */
} BwPortBattleReturnArgs;

typedef struct BwPortBattleReturnWork {
    void *nameInArgs;         /* 0x00 */
    void *nameBuf;            /* 0x04 */
    void *caughtMon;          /* 0x08 */
    BwPortCatchArgs catchArgs;/* 0x0C */
    void *boxFullMsg;         /* 0x2C */
    u32   boxNo;              /* 0x30 */
} BwPortBattleReturnWork;

BW_PORT_STATIC_ASSERT(battle_return_work, sizeof(void *) != 4 ||
                      offsetof(BwPortBattleReturnWork, boxFullMsg) == 0x2Cu);
BW_PORT_STATIC_ASSERT(battle_return_box, sizeof(void *) != 4 ||
                      offsetof(BwPortBattleReturnWork, boxNo) == 0x30u);

/*
 * The battle record's own head, copied from the game's header (aletheia
 * src/game/ov093/btl_pokeparam_types.h).  The experience and the level
 * the battle server works with are in it.
 */
typedef struct BwPortBtlPokeParam {
    void *mon;          /* 0x00 */
    void *illusionMon;  /* 0x04 */
    u32   exp;          /* 0x08 */
    u16   species;      /* 0x0C */
    u16   maxHp;        /* 0x0E */
    u16   hp;           /* 0x10 */
    u16   heldItem;     /* 0x12 */
    u16   consumedItem; /* 0x14 */
    u16   baseAbility;  /* 0x16 */
    u8    level;        /* 0x18 */
    u8    partyIndex;   /* 0x19 */
} BwPortBtlPokeParam;

BW_PORT_STATIC_ASSERT(btl_exp, sizeof(void *) != 4 ||
                      offsetof(BwPortBtlPokeParam, exp) == 0x08u);
BW_PORT_STATIC_ASSERT(btl_level, sizeof(void *) != 4 ||
                      offsetof(BwPortBtlPokeParam, level) == 0x18u);

/*
 * One entry of the battle server's experience share table, copied from the
 * game's header (aletheia src/game/ov093/btl_server_flow.c).
 */
typedef struct BwPortExpShareRecord {
    u32 exp;           /* 0x00 */
    u8  hasBonusFlag;  /* 0x04 */
    u8  pad05[7];      /* 0x05 */
} BwPortExpShareRecord;

BW_PORT_STATIC_ASSERT(exp_share, sizeof(void *) != 4 ||
                      sizeof(BwPortExpShareRecord) == 12u);

/*
 * The battle server flow's own head, copied from the game's header
 * (aletheia src/game/ov093/btl_server_flow_types.h).  Its second member is
 * the battle's main object, the one the port's state layer wants: the two
 * sides and their battle records live in it.
 */
typedef struct BwPortBtlServerFlow {
    void *server;      /* 0x00 */
    void *mainModule;  /* 0x04: the battle's main object */
} BwPortBtlServerFlow;

BW_PORT_STATIC_ASSERT(flow_main_module, sizeof(void *) != 4 ||
                      offsetof(BwPortBtlServerFlow, mainModule) == 0x04u);

/*
 * One entry of a species' level-up table, copied from the game's header
 * (aletheia src/game/arm9/poke_tool.c).  The table is 0x68 bytes and ends
 * with the 0xFFFF/0xFFFF pair.
 */
typedef struct BwPortLevelUpMove {
    u16 move;   /* 0x00 */
    u16 level;  /* 0x02 */
} BwPortLevelUpMove;

#define BW_PORT_LEARNSET_SIZE 0x68u

/*
 * The box screen's own task, copied from the game's header (aletheia
 * src/game/ov170/box2_types.h).  The current box, the held slot and the
 * work block are the ones the PC's own code reads.
 */
typedef struct BwPortBoxParam {
    void *gameData;   /* 0x00 */
    void *boxSave;    /* 0x04 */
    void *party;      /* 0x08 */
} BwPortBoxParam;

typedef struct BwPortBoxSeq {
    int   resetValue; /* 0x00 */
    int   param;      /* 0x04 */
    void *entry;      /* 0x08: the move the box screen is carrying out */
    u16   state;      /* 0x0C */
    u16   timer;      /* 0x0E */
} BwPortBoxSeq;

/*
 * The box screen's own work, copied from the game's header (aletheia
 * src/game/ov170/box2_types.h).  The sequence block holds the move the
 * screen is carrying out.
 */
typedef struct BwPortBoxObjSlot {
    void *obj;   /* 0x00 */
    void *aux;   /* 0x04 */
} BwPortBoxObjSlot;

typedef struct BwPortBoxWork {
    void *oamSys;                     /* 0x00 */
    BwPortBoxObjSlot oamSlots[9];     /* 0x04 */
    u32   keyRepeatDelay;             /* 0x4C */
    u32   keyRepeatInterval;          /* 0x50 */
    void *vblankTask;                 /* 0x54 */
    BwPortBoxSeq seq;                 /* 0x58 */
} BwPortBoxWork;

typedef struct BwPortBoxTask {
    BwPortBoxParam *params;   /* 0x00 */
    void           *procSys;  /* 0x04 */
    int             subState; /* 0x08 */
    void           *subArgs;  /* 0x0C */
    u16             subResult;/* 0x10 */
    u8              pad12;    /* 0x12 */
    u8              menuMode; /* 0x13 */
    u8              curBox;   /* 0x14 */
    u8              boxLimit; /* 0x15 */
    u8              cursorSlot;/* 0x16 */
    u8              heldSlot; /* 0x17 */
    u8              pad18[8]; /* 0x18 */
    u8              pad20[2]; /* 0x20 */
    int             pickedSlot;/* 0x24 */
    int             state;    /* 0x28 */
    BwPortBoxWork  *work;     /* 0x2C */
} BwPortBoxTask;

BW_PORT_STATIC_ASSERT(box_cur_box, sizeof(void *) != 4 ||
                      offsetof(BwPortBoxTask, curBox) == 0x14u);
BW_PORT_STATIC_ASSERT(box_held_slot, sizeof(void *) != 4 ||
                      offsetof(BwPortBoxTask, heldSlot) == 0x17u);
BW_PORT_STATIC_ASSERT(box_work, sizeof(void *) != 4 ||
                      offsetof(BwPortBoxTask, work) == 0x2Cu);
BW_PORT_STATIC_ASSERT(box_seq, sizeof(void *) != 4 ||
                      offsetof(BwPortBoxWork, seq) == 0x58u);

/*
 * The move the box screen is carrying out, copied from the game's header
 * (aletheia src/game/ov170/box2_main.c).  The two slot numbers name the
 * move's ends: a number below 0x1E is a box slot, a number from 0x1E up is
 * a party slot, and a number from 0x24 up names a box of its own (the
 * number less 0x24).
 */
typedef struct BwPortBoxMove {
    u8  pad00[168];  /* 0x00: the seven movers */
    int count;       /* 0xA8 */
    u32 a;           /* 0xAC: the move's first end */
    u32 b;           /* 0xB0: the move's second end */
    u32 req;         /* 0xB4 */
} BwPortBoxMove;

/* The slot numbers the box screen uses. */
#define BW_PORT_BOX_PARTY_BASE 0x1Eu   /* the first party slot */
#define BW_PORT_BOX_LISTED_BASE 0x24u  /* the first box of its own */

/* ------------------------------------------------------------------ */
/* The helpers                                                         */
/* ------------------------------------------------------------------ */

/*
 * A reference to a Pokemon, filled the way the port's state layer reads
 * one (sdk/mod_state.c): the place, the side, the box and the slot name
 * the Pokemon, and the handle is the game's own record as far as the ABI's
 * u32 holds it.  On a host whose pointers are eight bytes the address does
 * not fit in it; the state layer owns what the handle means, and the place
 * and the slot name the Pokemon either way.
 */
static void mon_ref(BwMonRef *out, u8 place, u8 side, u8 box, u8 slot,
                    const void *record)
{
    out->handle = (u32)(uintptr_t)record;
    out->place = place;
    out->side = side;
    out->box = box;
    out->slot = slot;
}

/* The zone the player is in, through the port's own state calls. */
static u16 current_zone(void)
{
    const BwModState *state = bw_mod_state_table();

    if (state == NULL || state->zone == NULL) {
        return 0u;
    }
    return state->zone();
}

/*
 * The game data the port has learned, and the party of the running game.
 * The area binds the game data at the rows that carry it (the ones that
 * give the player a Pokemon), and the state layer works on it from then on
 * (sdk/mod_state.h).
 */
static void bind_game_data(void *gameData)
{
    if (gameData != NULL) {
        bw_mod_state_set_game_data(gameData);
    }
}

static struct Party *party_of(void *gameData)
{
    if (gameData == NULL || GAME_DATA_GetMyParty == NULL) {
        return NULL;
    }
    return (struct Party *)GAME_DATA_GetMyParty((const struct GameData *)gameData);
}

/* The party's own record at a slot, or NULL. */
static struct PokemonParam *party_member(struct Party *party, u32 slot)
{
    if (party == NULL || POKEPARTY_GetMember == NULL) {
        return NULL;
    }
    return POKEPARTY_GetMember(party, slot);
}

/* How many Pokemon the party holds. */
static u32 party_size(struct Party *party)
{
    if (party == NULL || POKEPARTY_GetCount == NULL) {
        return 0u;
    }
    return POKEPARTY_GetCount(party);
}

/*
 * The party slot a record sits in, or 0 when the party is not carried by
 * the call and the record cannot be matched against it.  The port's state
 * table names a party member by its slot, not by its record, so the match
 * needs the party itself.
 */
static u32 party_slot_of(struct Party *party, const struct PokemonParam *mon)
{
    u32 count;
    u32 i;

    if (party == NULL || mon == NULL) {
        return 0u;
    }
    count = party_size(party);
    if (count > 6u) {
        count = 6u;
    }
    for (i = 0; i < count; i++) {
        if (party_member(party, i) == mon) {
            return i;
        }
    }
    return 0u;
}

/*
 * The script's own work value, read the way the game reads it
 * (src/matched/ov/ov010_func_0215901C.c and
 * src/matched/ov/ov010_func_02158FE0.c): an id below 0x4000 is the value
 * itself, an id from 0x4000 up is a variable of the event work, and an id
 * from 0x8000 up is one of the script's own local variables.
 *
 * The event work's variables are read through the game's own accessor,
 * which the port calls with the game data the script command carries.
 */
static u32 script_work_value(const void *scriptWork, void *gameData, u32 id)
{
    if (id < 0x4000u) {
        return id;              /* a literal */
    }
    if (id < 0x8000u) {
        u8 *eventWork;
        u16 *value;

        if (gameData == NULL || GAME_DATA_GetEventWork == NULL ||
            ZONEDATA_GetEventWorkAddress == NULL) {
            return 0u;
        }
        eventWork = GAME_DATA_GetEventWork((const struct GameData *)gameData);
        if (eventWork == NULL) {
            return 0u;
        }
        value = ZONEDATA_GetEventWorkAddress((struct EventWork *)eventWork, id);
        if (value == NULL) {
            return 0u;
        }
        return (u32)*value;
    }
    if (id < 0xC000u) {
        const BwPortScriptWork *work = (const BwPortScriptWork *)scriptWork;

        if (work == NULL) {
            return 0u;
        }
        return (u32)work->localWorks[id - 0x8000u];
    }
    return 0u;
}

/*
 * The n-th operand of the script command that is running, read without
 * moving the script VM's program counter: the game's own
 * SCRIPT_WORK_ReadWorkValue (0x02159B10) consumes the counter, so the port
 * reads the operand's id itself and resolves it.
 */
static u32 script_operand(uintptr_t vm, const void *work, u32 index)
{
    const BwPortScrcmdWork *cmd = (const BwPortScrcmdWork *)work;
    const BwPortScriptVm *machine = (const BwPortScriptVm *)vm;
    const BwPortScrcmdCommon *common;
    u32 id;

    if (machine == NULL || cmd == NULL || cmd->common == NULL ||
        machine->pc == NULL) {
        return 0u;
    }
    id = (u32)machine->pc[index * 2u] | ((u32)machine->pc[index * 2u + 1u] << 8);
    common = cmd->common;
    return script_work_value(common->scriptWork, common->gameData, id);
}

/*
 * The move a Pokemon learns at its level, read out of the species' own
 * level-up table the way the game reads it (src/game/arm9/poke_tool.c,
 * POKE_TOOL_LearnLevelUpMove): the table is loaded from the species data
 * and scanned from the caller's own index for the entry of the Pokemon's
 * level.  The move is 0 when the table holds no such entry.
 */
static u16 level_up_move(struct PokemonParam *mon, const u32 *tableIndex)
{
    BwPortLevelUpMove table[BW_PORT_LEARNSET_SIZE / sizeof(BwPortLevelUpMove)];
    u32 species;
    u32 form;
    u32 level;
    u32 index;
    u32 i;

    if (mon == NULL || POKE_TOOL_GetPartyValue == NULL ||
        POKE_PERSONAL_LoadDataDirect == NULL) {
        return 0u;
    }
    species = POKE_TOOL_GetPartyValue(mon, BW_PORT_FIELD_SPECIES, 0u);
    form = POKE_TOOL_GetPartyValue(mon, BW_PORT_FIELD_FORM, 0u);
    level = POKE_TOOL_GetPartyValue(mon, BW_PORT_FIELD_LEVEL, 0u);
    POKE_PERSONAL_LoadDataDirect(species, (int)form, table);
    index = (tableIndex != NULL) ? *tableIndex : 0u;
    for (i = index; i < BW_PORT_LEARNSET_SIZE / sizeof(BwPortLevelUpMove); i++) {
        if (table[i].move == 0xFFFFu && table[i].level == 0xFFFFu) {
            break;          /* the end of the table */
        }
        if (table[i].level == level) {
            return table[i].move;
        }
    }
    return 0u;
}

/*
 * The move slot a Pokemon's new move goes into: the first of its four move
 * slots that is empty, or 0xFF when it holds four moves already and the
 * move has to replace one.
 */
static u8 free_move_slot(struct PokemonParam *mon)
{
    u32 i;

    if (mon == NULL || POKE_TOOL_GetPartyValue == NULL) {
        return 0xFFu;
    }
    for (i = 0; i < 4u; i++) {
        if (POKE_TOOL_GetPartyValue(mon, BW_PORT_FIELD_MOVE1 + i, 0u) == 0u) {
            return (u8)i;
        }
    }
    return 0xFFu;
}

/*
 * The method an evolution happens by (BW_EVOLVE_*).  The mode the check
 * runs in names the context (0 a level up, 1 a trade, 2 and 3 an item),
 * and the method the check writes out names the rule of the row it found.
 */
static u8 evolve_method(u32 mode, const u32 *methodOut)
{
    u32 method = (methodOut != NULL) ? *methodOut : 0u;

    if (mode == 1u) {
        return (u8)BW_EVOLVE_TRADE;
    }
    if (mode == 2u || mode == 3u) {
        return (u8)BW_EVOLVE_ITEM;
    }
    switch (method) {
    case 1u:                 /* the friendship of the row */
    case 2u:
    case 3u:
        return (u8)BW_EVOLVE_FRIENDSHIP;
    case 19u:                /* the held item of the row */
    case 20u:
        return (u8)BW_EVOLVE_ITEM;
    default:
        return (u8)BW_EVOLVE_LEVEL;
    }
}

/* ------------------------------------------------------------------ */
/* The builders                                                        */
/* ------------------------------------------------------------------ */

/*
 * mon.create: the Pokemon is created (POKE_TOOL_CreateEx).  The species
 * and the level are the call's own arguments, and the personality is the
 * one the caller rolled with POKE_TOOL_CreatePersonalRnd and handed over.
 *
 * The personality is a 64-bit argument, and the call carries it as one
 * word: the low half is the personality value the game stores (the high
 * half is the game's own sentinel), so the payload carries the low half.
 * When the caller passes the sentinel (the game rolls the personality
 * inside the call), the payload's personality reads 0.
 *
 * The shiny flag is a request (1 forces a shiny Pokemon, 0 leaves the
 * roll), and the roll is the caller's: the port does not repeat it here.
 * The nature is rolled inside the call as well (GFL_USE_GetRandom(0x19)
 * in POKE_TOOL_SetupBoxParamEx), so the payload carries the document's
 * sentinel 0xFF, "leave the roll".
 */
static void build_mon_create(const BwEventCall *call, BwEventPayload *out)
{
    memset(&out->mon_create, 0, sizeof(out->mon_create));
    out->mon_create.species = (u16)call->a[0];
    out->mon_create.level = (u8)call->a[1];
    out->mon_create.personality = (u32)call->a[4];
    out->mon_create.shiny = 0u;
    out->mon_create.nature = 0xFFu;
}

/*
 * mon.create: the personality is rolled (POKE_TOOL_CreatePersonalRnd).
 * The species is the call's argument and the shiny flag is the call's own
 * request: the game forces a shiny Pokemon through the id key when the
 * caller asks for it (the function's mode 1) and leaves the roll alone
 * when the caller does not.
 *
 * The personality itself is rolled inside the call, so it is not in the
 * payload here: it is in the payload of the POKE_TOOL_CreateEx row, which
 * carries it as the argument the caller passes on.
 *
 * The nature is not in the payload here either: it is rolled inside the
 * creation call, and the payload carries the document's sentinel 0xFF,
 * "leave the roll".
 */
static void build_mon_create_personal(const BwEventCall *call,
                                      BwEventPayload *out)
{
    memset(&out->mon_create, 0, sizeof(out->mon_create));
    out->mon_create.species = (u16)call->a[1];
    out->mon_create.level = 0u;
    out->mon_create.personality = 0u;
    out->mon_create.shiny = (call->a[5] == 1u) ? 1u : 0u;
    out->mon_create.nature = 0xFFu;
}

/*
 * mon.create: the species and the level a handler changed are the ones the
 * game builds the record from, so the applier writes them back into the
 * call's arguments.
 */
static void apply_mon_create(const BwEventPayload *in, BwEventCall *call)
{
    call->a[0] = (uintptr_t)in->mon_create.species;
    call->a[1] = (uintptr_t)in->mon_create.level;
}

/*
 * mon.create: a handler that asks for a shiny Pokemon gets one.  The
 * game's own forced-shiny mode is the call's last argument (1 forces a
 * shiny Pokemon through the id key, 0 forces a dull one and 2 leaves the
 * roll), so the applier writes that argument.  A handler that leaves the
 * flag at 0 leaves the roll as the caller asked for it.
 */
static void apply_mon_create_personal(const BwEventPayload *in,
                                      BwEventCall *call)
{
    if (in->mon_create.shiny == 1u) {
        call->a[5] = 1u;
    }
}

/*
 * mon.receive: a Pokemon was caught (the battle screen's store step,
 * EVENT_BATTLE_RETURN_Main).  The function is a step machine and the slot
 * fires at every step of it, so the port fills the payload at the store
 * step only (the third one), where the caught Pokemon goes into the party
 * or into a box.
 *
 * The caught Pokemon is the screen's own record, and the box it went to is
 * the one the screen found free when the party was full.  The free slot
 * inside that box is not named by the call, so the payload's slot reads 0
 * for a Pokemon that went into a box.
 */
static void build_mon_receive_catch(const BwEventCall *call,
                                    BwEventPayload *out)
{
    const BwPortBattleReturnArgs *args =
        (const BwPortBattleReturnArgs *)call->a[2];
    const BwPortBattleReturnWork *work =
        (const BwPortBattleReturnWork *)call->a[3];
    const u32 *seq = (const u32 *)call->a[1];
    struct PokemonParam *mon;
    struct Party *party;
    u32 count;

    memset(&out->mon_receive, 0, sizeof(out->mon_receive));
    out->mon_receive.source = (u8)BW_RECEIVE_CATCH;
    if (seq == NULL || work == NULL || *seq != 3u) {
        return;     /* another step of the screen: nothing was stored yet */
    }
    mon = (struct PokemonParam *)work->caughtMon;
    if (mon == NULL) {
        return;
    }
    if (args != NULL) {
        bind_game_data(args->gameData);
    }
    out->mon_receive.species = (u16)POKE_TOOL_GetPartyValue(mon,
                                                             BW_PORT_FIELD_SPECIES, 0u);
    out->mon_receive.level = (u8)POKE_TOOL_GetPartyValue(mon,
                                                          BW_PORT_FIELD_LEVEL, 0u);
    out->mon_receive.shiny = (u8)(POKE_TOOL_IsShiny(mon) != 0u);
    out->mon_receive.zone = current_zone();
    mon_ref(&out->mon_receive.mon, (u8)BW_MON_PARTY, 0u, 0u, 0u, mon);
    if (work->boxFullMsg != NULL) {
        /* The party was full: the Pokemon went into a box. */
        out->mon_receive.to_box = 1u;
        out->mon_receive.mon.place = (u8)BW_MON_BOX;
        out->mon_receive.mon.box = (u8)work->boxNo;
    } else {
        party = party_of(args != NULL ? args->gameData : NULL);
        count = party_size(party);
        if (count > 0u) {
            out->mon_receive.mon.slot = (u8)(count - 1u);
        }
    }
}

/*
 * mon.receive: a Pokemon was given to the player (the field script's
 * SCRCMD_PROC_AddMonToParty).  The Pokemon is the party's last member
 * after the call, and the game data the call carries is the game's live
 * one, so the port binds it here: the state calls work on it from then on.
 */
static void build_mon_receive_party(const BwEventCall *call,
                                    BwEventPayload *out)
{
    void *gameData = (void *)call->a[0];
    struct Party *party;
    struct PokemonParam *mon;
    u32 count;

    memset(&out->mon_receive, 0, sizeof(out->mon_receive));
    out->mon_receive.source = (u8)BW_RECEIVE_GIFT;
    bind_game_data(gameData);
    party = party_of(gameData);
    count = party_size(party);
    if (count == 0u) {
        return;
    }
    mon = party_member(party, count - 1u);
    if (mon == NULL) {
        return;
    }
    mon_ref(&out->mon_receive.mon, (u8)BW_MON_PARTY, 0u, 0u,
            (u8)(count - 1u), mon);
    out->mon_receive.species = (u16)POKE_TOOL_GetPartyValue(mon,
                                                             BW_PORT_FIELD_SPECIES, 0u);
    out->mon_receive.level = (u8)POKE_TOOL_GetPartyValue(mon,
                                                          BW_PORT_FIELD_LEVEL, 0u);
    out->mon_receive.shiny = (u8)(POKE_TOOL_IsShiny(mon) != 0u);
    out->mon_receive.zone = current_zone();
}

/*
 * mon.receive: a Pokemon was given to the player and the party was full,
 * so it went into a box (the field script's SCRCMD_PROC_AddMonToBox).
 *
 * The game frees the record it built before the port sees the call, so the
 * species and the level come from the script's own creation request, which
 * the call carries.  The box slot the game chose is not named by the call
 * and the record is gone, so the payload's slot reads 0 and the shiny flag
 * reads 0 as well: nothing of the record is left to read.
 */
static void build_mon_receive_box(const BwEventCall *call,
                                  BwEventPayload *out)
{
    const BwPortPokemonSpec *spec = (const BwPortPokemonSpec *)call->a[1];

    memset(&out->mon_receive, 0, sizeof(out->mon_receive));
    out->mon_receive.source = (u8)BW_RECEIVE_GIFT;
    out->mon_receive.to_box = 1u;
    bind_game_data((void *)call->a[0]);
    if (spec != NULL) {
        out->mon_receive.species = (u16)spec->species;
        out->mon_receive.level = (u8)spec->level;
    }
    out->mon_receive.shiny = 0u;
    out->mon_receive.zone = current_zone();
    mon_ref(&out->mon_receive.mon, (u8)BW_MON_BOX, 0u, 0u, 0u, NULL);
}

/*
 * mon.receive: an egg was given to the player (the field script's
 * SCRCMD_PROC_AddEggToParty).  The egg is the party's last member after
 * the call, and the game data comes from the script command's own work.
 */
static void build_mon_receive_egg(const BwEventCall *call,
                                  BwEventPayload *out)
{
    const BwPortScrcmdWork *cmd = (const BwPortScrcmdWork *)call->a[1];
    const BwPortScrcmdCommon *common;
    struct Party *party;
    struct PokemonParam *mon;
    u32 count;

    memset(&out->mon_receive, 0, sizeof(out->mon_receive));
    out->mon_receive.source = (u8)BW_RECEIVE_EGG;
    if (cmd == NULL || cmd->common == NULL) {
        return;
    }
    common = cmd->common;
    bind_game_data(common->gameData);
    party = party_of(common->gameData);
    count = party_size(party);
    if (count == 0u) {
        return;
    }
    mon = party_member(party, count - 1u);
    if (mon == NULL) {
        return;
    }
    mon_ref(&out->mon_receive.mon, (u8)BW_MON_PARTY, 0u, 0u,
            (u8)(count - 1u), mon);
    out->mon_receive.species = (u16)POKE_TOOL_GetPartyValue(mon,
                                                             BW_PORT_FIELD_SPECIES, 0u);
    out->mon_receive.level = (u8)POKE_TOOL_GetPartyValue(mon,
                                                          BW_PORT_FIELD_LEVEL, 0u);
    out->mon_receive.shiny = (u8)(POKE_TOOL_IsShiny(mon) != 0u);
    out->mon_receive.zone = current_zone();
}

/*
 * mon.receive: a gift Pokemon was given to the player (the field script's
 * SCRCMD_POKEMON_FLD_ReceiveGiftMon).  The game data is the call's second
 * argument, and the Pokemon is the party's last member after the call.
 */
static void build_mon_receive_gift(const BwEventCall *call,
                                   BwEventPayload *out)
{
    void *gameData = (void *)call->a[1];
    struct Party *party;
    struct PokemonParam *mon;
    u32 count;

    memset(&out->mon_receive, 0, sizeof(out->mon_receive));
    out->mon_receive.source = (u8)BW_RECEIVE_GIFT;
    bind_game_data(gameData);
    party = party_of(gameData);
    count = party_size(party);
    if (count == 0u) {
        return;
    }
    mon = party_member(party, count - 1u);
    if (mon == NULL) {
        return;
    }
    mon_ref(&out->mon_receive.mon, (u8)BW_MON_PARTY, 0u, 0u,
            (u8)(count - 1u), mon);
    out->mon_receive.species = (u16)POKE_TOOL_GetPartyValue(mon,
                                                             BW_PORT_FIELD_SPECIES, 0u);
    out->mon_receive.level = (u8)POKE_TOOL_GetPartyValue(mon,
                                                          BW_PORT_FIELD_LEVEL, 0u);
    out->mon_receive.shiny = (u8)(POKE_TOOL_IsShiny(mon) != 0u);
    out->mon_receive.zone = current_zone();
}

/*
 * mon.receive: an egg came back from the day care (SODATEYA_ReceiveEgg).
 * The party is the call's second argument, and the egg is its last member
 * after the call.
 */
static void build_mon_receive_daycare_egg(const BwEventCall *call,
                                           BwEventPayload *out)
{
    struct Party *party = (struct Party *)call->a[1];
    struct PokemonParam *mon;
    u32 count;

    memset(&out->mon_receive, 0, sizeof(out->mon_receive));
    out->mon_receive.source = (u8)BW_RECEIVE_EGG;
    count = party_size(party);
    if (count == 0u) {
        return;
    }
    mon = party_member(party, count - 1u);
    if (mon == NULL) {
        return;
    }
    mon_ref(&out->mon_receive.mon, (u8)BW_MON_PARTY, 0u, 0u,
            (u8)(count - 1u), mon);
    out->mon_receive.species = (u16)POKE_TOOL_GetPartyValue(mon,
                                                             BW_PORT_FIELD_SPECIES, 0u);
    out->mon_receive.level = (u8)POKE_TOOL_GetPartyValue(mon,
                                                          BW_PORT_FIELD_LEVEL, 0u);
    out->mon_receive.shiny = (u8)(POKE_TOOL_IsShiny(mon) != 0u);
    out->mon_receive.zone = current_zone();
}

/*
 * mon.receive: an in-game trade was set up (FLD_TRADE_CreateEvent).  The
 * Pokemon the player gives is the party member of the call's third
 * argument, and the game system of the first one carries the game data.
 *
 * The species the trade offers is in the trade record, which is the
 * overlay's own data and is not named by the call, so the payload's
 * species reads 0: a mod reads the Pokemon it is given through the state
 * calls.
 */
static void build_mon_receive_trade(const BwEventCall *call,
                                     BwEventPayload *out)
{
    void *gameSystem = (void *)call->a[0];
    u32 partyIndex = (u32)call->a[2];
    struct GameData *gameData;
    struct Party *party;
    struct PokemonParam *mon;

    memset(&out->mon_receive, 0, sizeof(out->mon_receive));
    out->mon_receive.source = (u8)BW_RECEIVE_TRADE;
    if (gameSystem == NULL || GAMESYSTEM_GetGameData == NULL) {
        return;
    }
    gameData = GAMESYSTEM_GetGameData((const struct GameSystem *)gameSystem);
    bind_game_data(gameData);
    party = party_of(gameData);
    mon = party_member(party, partyIndex);
    if (mon == NULL) {
        return;
    }
    mon_ref(&out->mon_receive.mon, (u8)BW_MON_PARTY, 0u, 0u,
            (u8)partyIndex, mon);
    out->mon_receive.species = 0u;
    out->mon_receive.level = (u8)POKE_TOOL_GetPartyValue(mon,
                                                          BW_PORT_FIELD_LEVEL, 0u);
    out->mon_receive.shiny = (u8)(POKE_TOOL_IsShiny(mon) != 0u);
    out->mon_receive.zone = current_zone();
}

/*
 * mon.nickname: the name prompt opens (EVENT_NAMEIN_Create).  The call
 * carries the game system, the field map, the parent event and the mode,
 * but not the Pokemon: the prompt's own work holds it, and the work is
 * the overlay's own block.  The payload's mon reads 0.
 *
 * The prompt is opened by this call, so there is nothing for a handler to
 * force here: the game decides to ask before it gets here (the catch
 * screen asks whether the player wants a nickname, and that code is not a
 * declared row), and the port has no hook at that decision.
 */
static void build_mon_nickname(const BwEventCall *call, BwEventPayload *out)
{
    (void)call;
    memset(&out->mon_nickname, 0, sizeof(out->mon_nickname));
}

/*
 * mon.nickname: the nickname screen after a catch (ZUKAN_NICKNAME_Create).
 * The call carries the heap and the screen's own words, not the Pokemon:
 * the caught Pokemon is in the catch screen's work.  The payload's mon
 * reads 0, and the force flag reads 0: the screen is being opened here,
 * so there is nothing left for a handler to force.
 */
static void build_mon_nickname_catch(const BwEventCall *call,
                                     BwEventPayload *out)
{
    (void)call;
    memset(&out->mon_nickname, 0, sizeof(out->mon_nickname));
}

/*
 * mon.evolve: the evolution check ran (SHINKA_CHECK_CheckEvolution).  The
 * event fires after the check, because the species the Pokemon evolves
 * into is the check's own result: the caller reads it and evolves the
 * Pokemon into it, so a handler's change to the target species has to
 * reach the result.
 *
 * The Pokemon is the check's second argument and the party is its first,
 * so the port can name the party position.  The method comes from the
 * context the check ran in and from the method the check wrote out.
 */
static void build_mon_evolve(const BwEventCall *call, BwEventPayload *out)
{
    struct Party *party = (struct Party *)call->a[0];
    struct PokemonParam *mon = (struct PokemonParam *)call->a[1];
    u32 mode = (u32)call->a[2];
    const u32 *methodOut = (const u32 *)call->a[5];

    memset(&out->mon_evolve, 0, sizeof(out->mon_evolve));
    if (mon == NULL) {
        return;
    }
    mon_ref(&out->mon_evolve.mon, (u8)BW_MON_PARTY, 0u, 0u,
            (u8)party_slot_of(party, mon), mon);
    out->mon_evolve.from = (u16)POKE_TOOL_GetPartyValue(mon,
                                                         BW_PORT_FIELD_SPECIES, 0u);
    out->mon_evolve.to = (u16)call->ret;
    out->mon_evolve.method = evolve_method(mode, methodOut);
}

/*
 * mon.evolve: the species a handler changed is the species the caller
 * evolves the Pokemon into, so the applier writes the result the check
 * answered.
 */
static void apply_mon_evolve(const BwEventPayload *in, BwEventCall *call)
{
    if (in->mon_evolve.to != 0u) {
        call->ret = (s64)in->mon_evolve.to;
    }
}

/*
 * mon.evolve: the refused outcome is no evolution: the check's result is
 * 0 in all ten of its callers when there is no evolution (docs/mod-events.md,
 * section 1, "Pokemon"), and an evolution stone has no effect.
 */
static void refuse_mon_evolve(BwEventCall *call)
{
    call->ret = 0;
}

/*
 * mon.exp: the battle server worked out the experience shares (the return
 * of BTL_SERVER_FLOW_CalcPartyExpShares).  The share table is the call's
 * fourth argument: one entry per party position, with the experience that
 * position gets.
 *
 * The payload names one Pokemon: the first member of the party that gains
 * experience.  The battle works on a copy, so the experience and the level
 * the payload carries are the party record's own, read through the port's
 * state calls: they are the values the battle started from.
 */
static void build_mon_exp_battle(const BwEventCall *call,
                                 BwEventPayload *out)
{
    const BwPortBtlServerFlow *flow =
        (const BwPortBtlServerFlow *)call->a[0];
    const BwPortExpShareRecord *table =
        (const BwPortExpShareRecord *)call->a[3];
    const BwModState *state = bw_mod_state_table();
    BwMonRef ref;
    u32 value;
    u32 i;

    memset(&out->mon_exp, 0, sizeof(out->mon_exp));

    /*
     * The battle's main object: the flow carries it, and the port's state
     * layer works on it (the two sides and their battle records live in
     * it).  This is where the pokemon area learns the battle; the battle
     * area's own rows bind it at the battle's start and drop it at the
     * battle's end.
     */
    if (flow != NULL && flow->mainModule != NULL) {
        bw_mod_state_set_battle(flow->mainModule);
    }

    if (table == NULL) {
        return;
    }
    for (i = 0; i < 6u; i++) {
        if (table[i].exp != 0u) {
            break;
        }
    }
    if (i >= 6u) {
        return;     /* nobody gains experience */
    }
    mon_ref(&out->mon_exp.mon, (u8)BW_MON_BATTLE, 0u, 0u, (u8)i, NULL);
    out->mon_exp.amount = table[i].exp;
    if (state != NULL && state->party_ref != NULL && state->party_get != NULL) {
        if (state->party_ref(i, &ref) == 0) {
            if (state->party_get(ref, BW_MON_FIELD_EXP, &value) == 0) {
                out->mon_exp.exp = value;
            }
            if (state->party_get(ref, BW_MON_FIELD_LEVEL, &value) == 0) {
                out->mon_exp.level = (u8)value;
            }
        }
    }
}

/*
 * mon.exp: the experience a handler changed is the experience the battle
 * server hands out, so the applier writes the share the payload names.
 */
static void apply_mon_exp_battle(const BwEventPayload *in, BwEventCall *call)
{
    BwPortExpShareRecord *table = (BwPortExpShareRecord *)call->a[3];

    if (table == NULL || in->mon_exp.amount == 0u) {
        return;
    }
    if (in->mon_exp.mon.slot < 6u) {
        table[in->mon_exp.mon.slot].exp = in->mon_exp.amount;
    }
}

/*
 * mon.exp: the day care hands out experience (SODATEYA_GrantExp).  The
 * Pokemon is the call's first argument and the amount is its second; the
 * experience and the level are the Pokemon's own, before the grant.
 *
 * The party is not carried by the call, so the payload's party position
 * reads 0: the port cannot match the record against the party from here.
 */
static void build_mon_exp_daycare(const BwEventCall *call,
                                  BwEventPayload *out)
{
    struct PokemonParam *mon = (struct PokemonParam *)call->a[0];

    memset(&out->mon_exp, 0, sizeof(out->mon_exp));
    if (mon == NULL) {
        return;
    }
    mon_ref(&out->mon_exp.mon, (u8)BW_MON_PARTY, 0u, 0u, 0u, mon);
    out->mon_exp.amount = (u32)call->a[1];
    out->mon_exp.exp = POKE_TOOL_GetPartyValue(mon, BW_PORT_FIELD_EXP, 0u);
    out->mon_exp.level = (u8)POKE_TOOL_GetPartyValue(mon, BW_PORT_FIELD_LEVEL, 0u);
}

/*
 * mon.exp: the amount a handler changed is the amount the day care grants.
 */
static void apply_mon_exp_daycare(const BwEventPayload *in, BwEventCall *call)
{
    call->a[1] = (uintptr_t)in->mon_exp.amount;
}

/*
 * mon.levelup: a Pokemon crossed a level (BTL_POKEPARAM_AddExp).  The
 * event fires after the call, because the call's own result says whether
 * it crossed a level (1 when it did), and the function is called once per
 * level it crosses.
 *
 * The battle record is the call's first argument: its level is the level
 * the Pokemon reached, and the level before is one lower, because the
 * function stops at the next level.  The party position is not carried by
 * the call; the record's own party index is the battle's id of that
 * Pokemon, and it is what the payload carries.
 */
static void build_mon_levelup(const BwEventCall *call, BwEventPayload *out)
{
    const BwPortBtlPokeParam *record =
        (const BwPortBtlPokeParam *)call->a[0];

    memset(&out->mon_levelup, 0, sizeof(out->mon_levelup));
    if (record == NULL || call->ret != 1) {
        return;     /* the call did not cross a level */
    }
    mon_ref(&out->mon_levelup.mon, (u8)BW_MON_BATTLE, 0u, 0u,
            record->partyIndex, record->mon);
    out->mon_levelup.from = (u8)(record->level > 0u ? record->level - 1u : 0u);
    out->mon_levelup.to = record->level;
}

/*
 * move.learn: a Pokemon learns a move by levelling up
 * (POKE_TOOL_GetLevelUpMove).  The function scans the species' level-up
 * table from the caller's own index for the entry of the Pokemon's level
 * and writes that move into the Pokemon, so the port reads the same table
 * and names the move the call is about to learn.
 *
 * The move slot is the first of the Pokemon's four slots that is empty,
 * or 0xFF when it holds four moves already and the game asks which one to
 * replace.  The move it replaces reads 0: the game asks for that later,
 * in the screen that owns the choice.
 */
static void build_move_learn_level(const BwEventCall *call,
                                   BwEventPayload *out)
{
    struct PokemonParam *mon = (struct PokemonParam *)call->a[0];
    const u32 *tableIndex = (const u32 *)call->a[1];

    memset(&out->move_learn, 0, sizeof(out->move_learn));
    if (mon == NULL) {
        return;
    }
    mon_ref(&out->move_learn.mon, (u8)BW_MON_PARTY, 0u, 0u, 0u, mon);
    out->move_learn.move = level_up_move(mon, tableIndex);
    out->move_learn.source = (u8)BW_MOVE_SOURCE_LEVEL;
    out->move_learn.slot = free_move_slot(mon);
    out->move_learn.replaced = 0u;
}

/*
 * move.learn: the refused outcome is that the move is skipped: the scan
 * goes past the entry and the call answers the "already known" code
 * (0xFFFE), which every caller of the function treats as nothing learned
 * (docs/mod-implementation.md, section 1, "Refusal functions").
 */
static void refuse_move_learn_level(BwEventCall *call)
{
    u32 *tableIndex = (u32 *)call->a[1];

    if (tableIndex != NULL) {
        (*tableIndex)++;
    }
    call->ret = 0xFFFE;
}

/*
 * move.learn: the day care teaches a move (SODATEYA_LearnMove).  The
 * Pokemon and the move are the call's own arguments, and the move slot is
 * the first of the Pokemon's four slots that is empty.
 */
static void build_move_learn_daycare(const BwEventCall *call,
                                     BwEventPayload *out)
{
    struct PokemonParam *mon = (struct PokemonParam *)call->a[0];

    memset(&out->move_learn, 0, sizeof(out->move_learn));
    if (mon == NULL) {
        return;
    }
    mon_ref(&out->move_learn.mon, (u8)BW_MON_PARTY, 0u, 0u, 0u, mon);
    out->move_learn.move = (u16)call->a[1];
    out->move_learn.source = (u8)BW_MOVE_SOURCE_LEVEL;
    out->move_learn.slot = free_move_slot(mon);
    out->move_learn.replaced = 0u;
}

/*
 * move.learn: the refused outcome is that the day care writes nothing:
 * the call is not made, so the Pokemon does not learn the move.
 */
static void refuse_move_learn_daycare(BwEventCall *call)
{
    call->ret = 0;
}

/*
 * mon.release: a Pokemon is released (BOX2_MAIN_ClearSlot).  The call
 * names the box and the slot: a number below 0x1E is a box slot and a
 * number from 0x1E up is a party position.  The Pokemon itself is the
 * record in that slot.
 *
 * The port fires the release outside the box update
 * (BOX2_MAIN_UpdateBoxSlots), because the box update clears slots of its
 * own: it removes the party member it moved into a box and the box slot it
 * moved into the party.  The release is the one the release step
 * (BOX2_SEQ_StateMoveItemF) asks for, and it happens outside the update.
 */
static void build_mon_release(const BwEventCall *call, BwEventPayload *out)
{
    const BwPortBoxTask *task = (const BwPortBoxTask *)call->a[0];
    u32 box = (u32)call->a[1];
    u32 slot = (u32)call->a[2];
    void *gameData;

    memset(&out->mon_release, 0, sizeof(out->mon_release));
    if (task == NULL || task->params == NULL) {
        return;
    }
    gameData = task->params->gameData;
    if (slot < BW_PORT_BOX_PARTY_BASE) {
        struct BoxPokemon *boxMon = NULL;

        if (gameData != NULL && BOX_SAVEDATA_GetPokemon != NULL) {
            boxMon = BOX_SAVEDATA_GetPokemon((struct GameData *)gameData,
                                             box, slot);
        }
        mon_ref(&out->mon_release.mon, (u8)BW_MON_BOX, 0u, (u8)box,
                (u8)slot, boxMon);
        if (boxMon != NULL && POKE_TOOL_GetBoxValue != NULL) {
            out->mon_release.species = (u16)POKE_TOOL_GetBoxValue(boxMon,
                                                                  BW_PORT_FIELD_SPECIES, 0u);
        }
    } else {
        struct PokemonParam *mon = party_member(task->params->party,
                                                 slot - BW_PORT_BOX_PARTY_BASE);

        mon_ref(&out->mon_release.mon, (u8)BW_MON_PARTY, 0u, 0u,
                (u8)(slot - BW_PORT_BOX_PARTY_BASE), mon);
        if (mon != NULL) {
            out->mon_release.species = (u16)POKE_TOOL_GetPartyValue(mon,
                                                                     BW_PORT_FIELD_SPECIES, 0u);
        }
    }
}

/*
 * mon.release: the refused outcome is that the Pokemon stays: the slot is
 * not cleared, and the release step redraws the slot itself
 * (docs/mod-implementation.md, section 1, "Refusal functions").
 */
static void refuse_mon_release(BwEventCall *call)
{
    call->ret = 0;
}

/*
 * box.deposit: a party Pokemon is put into a box
 * (BOX2_MAIN_UpdateBoxSlots).  The move the box screen carries names its
 * two ends: the first is where the Pokemon comes from, the second where it
 * goes.  A number below 0x1E is a box slot, a number from 0x1E up is a
 * party position, and a number from 0x24 up names a box of its own.
 *
 * The payload names the Pokemon where it is now (in the party) and the box
 * slot it moves into.
 */
static void build_box_deposit(const BwEventCall *call, BwEventPayload *out)
{
    const BwPortBoxTask *task = (const BwPortBoxTask *)call->a[0];
    const BwPortBoxMove *move;
    struct PokemonParam *mon;
    u32 from;
    u32 to;
    u32 box;
    u32 slot;

    memset(&out->box_deposit, 0, sizeof(out->box_deposit));
    if (task == NULL || task->work == NULL || task->work->seq.entry == NULL) {
        return;
    }
    move = (const BwPortBoxMove *)task->work->seq.entry;
    from = move->a;
    to = move->b;
    if (to >= BW_PORT_BOX_LISTED_BASE) {
        /* A move into a box of its own: the Pokemon is the held slot and
         * it goes into that box at the slot the move names. */
        from = task->heldSlot;
        box = to - BW_PORT_BOX_LISTED_BASE;
        slot = move->a;
    } else if (from >= BW_PORT_BOX_PARTY_BASE && to < BW_PORT_BOX_PARTY_BASE) {
        box = task->curBox;
        slot = to;
    } else {
        return;     /* not a deposit */
    }
    if (from < BW_PORT_BOX_PARTY_BASE) {
        return;     /* the held Pokemon is in a box: nothing is deposited */
    }
    mon = party_member(task->params != NULL ? task->params->party : NULL,
                       from - BW_PORT_BOX_PARTY_BASE);
    mon_ref(&out->box_deposit.mon, (u8)BW_MON_PARTY, 0u, 0u,
            (u8)(from - BW_PORT_BOX_PARTY_BASE), mon);
    out->box_deposit.box = (u8)box;
    out->box_deposit.slot = (u8)slot;
}

/*
 * box.withdraw: a box Pokemon is put into the party
 * (BOX2_MAIN_UpdateBoxSlots).  The Pokemon is the box slot the move names,
 * and the payload names the box and the slot it comes out of.
 */
static void build_box_withdraw(const BwEventCall *call, BwEventPayload *out)
{
    const BwPortBoxTask *task = (const BwPortBoxTask *)call->a[0];
    const BwPortBoxMove *move;
    struct BoxPokemon *boxMon = NULL;
    void *gameData;
    u32 from;
    u32 to;

    memset(&out->box_withdraw, 0, sizeof(out->box_withdraw));
    if (task == NULL || task->work == NULL || task->work->seq.entry == NULL ||
        task->params == NULL) {
        return;
    }
    move = (const BwPortBoxMove *)task->work->seq.entry;
    from = move->a;
    to = move->b;
    if (from >= BW_PORT_BOX_PARTY_BASE || to < BW_PORT_BOX_PARTY_BASE ||
        to >= BW_PORT_BOX_LISTED_BASE) {
        return;     /* not a withdrawal */
    }
    gameData = task->params->gameData;
    if (gameData != NULL && BOX_SAVEDATA_GetPokemon != NULL) {
        boxMon = BOX_SAVEDATA_GetPokemon((struct GameData *)gameData,
                                         task->curBox, from);
    }
    mon_ref(&out->box_withdraw.mon, (u8)BW_MON_BOX, 0u, task->curBox,
            (u8)from, boxMon);
    out->box_withdraw.box = task->curBox;
    out->box_withdraw.slot = (u8)from;
}

/*
 * box.deposit and box.withdraw: the refused outcome is that the move is
 * not applied, so the Pokemon stays where it was.  The icons of the box
 * are redrawn by the box screen's own next step; the redraw the game does
 * inside the move (BOX2_UI_DrawSlot) is the overlay's own function and the
 * port's refusal runs in the ARM9, where it cannot be called.
 */
static void refuse_box_move(BwEventCall *call)
{
    call->ret = 0;
}

/*
 * party.swap: two party positions change places (POKEPARTY_SwapMembers).
 * The two positions are the call's own arguments.  The row fires outside
 * the box update (BOX2_MAIN_UpdateBoxSlots), which fires its own events
 * for the moves it makes between a box and the party.
 */
static void build_party_swap(const BwEventCall *call, BwEventPayload *out)
{
    memset(&out->party_swap, 0, sizeof(out->party_swap));
    out->party_swap.slot_a = (u8)call->a[1];
    out->party_swap.slot_b = (u8)call->a[2];
}

/*
 * party.swap: the refused outcome is that the order does not change: the
 * call is not made, so the two Pokemon stay where they are and the party
 * screen redraws the party from the party itself.
 */
static void refuse_party_swap(BwEventCall *call)
{
    call->ret = 0;
}

/*
 * mon.helditem: a Pokemon is given a held item (POKELIST_SetHeldItem).
 * The item is the call's third argument and the Pokemon is its second;
 * the item the Pokemon held before is the Pokemon's own field.
 *
 * The party is not carried by the call, so the payload's party position
 * reads 0.
 */
static void build_mon_helditem(const BwEventCall *call, BwEventPayload *out)
{
    struct PokemonParam *mon = (struct PokemonParam *)call->a[1];

    memset(&out->mon_helditem, 0, sizeof(out->mon_helditem));
    if (mon == NULL) {
        return;
    }
    mon_ref(&out->mon_helditem.mon, (u8)BW_MON_PARTY, 0u, 0u, 0u, mon);
    out->mon_helditem.item = (u16)call->a[2];
    out->mon_helditem.previous = (u16)POKE_TOOL_GetPartyValue(mon,
                                                              BW_PORT_FIELD_ITEM, 0u);
}

/*
 * egg.hatch: an egg is about to hatch (SCRCMD_POKEMON_FLD_StartEggHatch).
 * The command looks for the first egg of the party that is ready to hatch
 * and starts its hatch event, so the port looks for the same egg: the one
 * that is an egg, whose checksum is good and whose counter has run out.
 */
static void build_egg_hatch(const BwEventCall *call, BwEventPayload *out)
{
    const BwPortScrcmdWork *cmd = (const BwPortScrcmdWork *)call->a[1];
    const BwPortScrcmdCommon *common;
    struct Party *party;
    struct PokemonParam *mon;
    u32 count;
    u32 i;

    memset(&out->egg_hatch, 0, sizeof(out->egg_hatch));
    if (cmd == NULL || cmd->common == NULL) {
        return;
    }
    common = cmd->common;
    bind_game_data(common->gameData);
    party = party_of(common->gameData);
    count = party_size(party);
    for (i = 0; i < count; i++) {
        mon = party_member(party, i);
        if (mon == NULL) {
            continue;
        }
        if (POKE_TOOL_GetPartyValue(mon, BW_PORT_FIELD_IS_EGG, 0u) == 0u) {
            continue;
        }
        if (POKE_TOOL_GetPartyValue(mon, 0x03u, 0u) != 0u) {
            continue;   /* the checksum failed */
        }
        if (POKE_TOOL_GetPartyValue(mon, 0x09u, 0u) != 0u) {
            continue;   /* the counter has not run out yet */
        }
        mon_ref(&out->egg_hatch.mon, (u8)BW_MON_PARTY, 0u, 0u, (u8)i, mon);
        out->egg_hatch.species = (u16)POKE_TOOL_GetPartyValue(mon,
                                                               BW_PORT_FIELD_SPECIES, 0u);
        return;
    }
}

/*
 * trade.ingame: an in-game trade is about to happen
 * (SCRCMD_POKEMON_FLD_StartTradeEvent).  The trade and the party position
 * are the command's own operands, and the port reads them out of the
 * script's variables without moving the script VM's program counter.
 *
 * The species the trade offers is in the trade record, which is the
 * overlay's own data and is not named by the call, so the payload's
 * received species reads 0.
 */
static void build_trade_ingame(const BwEventCall *call, BwEventPayload *out)
{
    const BwPortScrcmdWork *cmd = (const BwPortScrcmdWork *)call->a[1];
    const BwPortScrcmdCommon *common;
    struct Party *party;
    struct PokemonParam *mon;
    u32 tradeId;
    u32 partySlot;

    memset(&out->trade_ingame, 0, sizeof(out->trade_ingame));
    if (cmd == NULL || cmd->common == NULL) {
        return;
    }
    common = cmd->common;
    tradeId = script_operand(call->a[0], cmd, 0u);
    partySlot = script_operand(call->a[0], cmd, 1u);
    out->trade_ingame.trade_id = (u16)tradeId;
    bind_game_data(common->gameData);
    party = party_of(common->gameData);
    mon = party_member(party, partySlot);
    if (mon != NULL) {
        mon_ref(&out->trade_ingame.given, (u8)BW_MON_PARTY, 0u, 0u,
                (u8)partySlot, mon);
    }
    out->trade_ingame.received_species = 0u;
}

/*
 * trade.ingame: the refused outcome is that the trade does not happen:
 * the call is not made, so the trade event is not started, and the script
 * goes on (docs/mod-implementation.md, section 1, "Refusal functions").
 */
static void refuse_trade_ingame(BwEventCall *call)
{
    call->ret = 1;
}

/* ------------------------------------------------------------------ */
/* The rows                                                            */
/* ------------------------------------------------------------------ */

/*
 * The rows of this area.
 *
 * The scope of a row is BW_SCOPE_ANY unless the documents name one:
 *
 *   - party.swap does not fire inside the box update
 *     (BOX2_MAIN_UpdateBoxSlots), which fires its own events for the moves
 *     it makes between a box and the party (docs/mod-events.md, section 1).
 *   - mon.levelup fires only inside the battle server's own experience
 *     step (BTL_SERVER_FLOW_ApplyPartyExp), which is where the level
 *     crossing happens.
 *   - mon.release fires outside the box update, for the reason the
 *     builder above gives: the update clears slots of its own.
 *
 * The point of a row is BW_EVENT_BEFORE, unless the payload carries the
 * result of the call, in which case the row fires after it:
 *
 *   - mon.receive fires after the Pokemon is stored (docs/mod-events.md,
 *     section 1, "Pokemon").
 *   - mon.evolve carries the species the check answers.
 *   - mon.exp's first row is the return of the share calculation, the way
 *     the document names it.
 *   - mon.levelup carries the result that says whether the call crossed a
 *     level.
 *
 * mon.create's two rows both fire before their function: the species, the
 * level and the personality are the arguments the caller passes on, and
 * the shiny request is the argument the caller asks the roll with.
 */
static const BwEventBinding g_mon_rows[] = {
    { BW_MOD_REGION_ARM9, 0x02017430u, BW_EVENT_MON_CREATE, BW_EVENT_BEFORE,
      BW_SCOPE_ANY, 0u, 0u, build_mon_create, apply_mon_create, NULL },

    { BW_MOD_REGION_ARM9, 0x020186C8u, BW_EVENT_MON_CREATE, BW_EVENT_BEFORE,
      BW_SCOPE_ANY, 0u, 0u, build_mon_create_personal, apply_mon_create_personal,
      NULL },

    { BW_MOD_REGION_OVL(92), 0x021B9614u, BW_EVENT_MON_RECEIVE, BW_EVENT_AFTER,
      BW_SCOPE_ANY, 0u, 0u, build_mon_receive_catch, NULL, NULL },

    { BW_MOD_REGION_OVL(10), 0x0215C510u, BW_EVENT_MON_RECEIVE, BW_EVENT_AFTER,
      BW_SCOPE_ANY, 0u, 0u, build_mon_receive_party, NULL, NULL },

    { BW_MOD_REGION_OVL(10), 0x0215C4C8u, BW_EVENT_MON_RECEIVE, BW_EVENT_AFTER,
      BW_SCOPE_ANY, 0u, 0u, build_mon_receive_box, NULL, NULL },

    { BW_MOD_REGION_OVL(10), 0x0215C2D0u, BW_EVENT_MON_RECEIVE, BW_EVENT_AFTER,
      BW_SCOPE_ANY, 0u, 0u, build_mon_receive_egg, NULL, NULL },

    { BW_MOD_REGION_OVL(21), 0x021B6F18u, BW_EVENT_MON_RECEIVE, BW_EVENT_AFTER,
      BW_SCOPE_ANY, 0u, 0u, build_mon_receive_gift, NULL, NULL },

    { BW_MOD_REGION_OVL(21), 0x021C32F4u, BW_EVENT_MON_RECEIVE, BW_EVENT_AFTER,
      BW_SCOPE_ANY, 0u, 0u, build_mon_receive_daycare_egg, NULL, NULL },

    { BW_MOD_REGION_OVL(21), 0x021CC500u, BW_EVENT_MON_RECEIVE, BW_EVENT_AFTER,
      BW_SCOPE_ANY, 0u, 0u, build_mon_receive_trade, NULL, NULL },

    { BW_MOD_REGION_OVL(21), 0x021CD2A4u, BW_EVENT_MON_NICKNAME, BW_EVENT_BEFORE,
      BW_SCOPE_ANY, 0u, 0u, build_mon_nickname, NULL, NULL },

    { BW_MOD_REGION_OVL(207), 0x0220B55Cu, BW_EVENT_MON_NICKNAME, BW_EVENT_BEFORE,
      BW_SCOPE_ANY, 0u, 0u, build_mon_nickname_catch, NULL, NULL },

    { BW_MOD_REGION_ARM9, 0x0201B2B0u, BW_EVENT_MON_EVOLVE, BW_EVENT_AFTER,
      BW_SCOPE_ANY, 0u, 0u, build_mon_evolve, apply_mon_evolve,
      refuse_mon_evolve },

    { BW_MOD_REGION_OVL(93), 0x021CB2D4u, BW_EVENT_MON_EXP, BW_EVENT_AFTER,
      BW_SCOPE_ANY, 0u, 0u, build_mon_exp_battle, apply_mon_exp_battle, NULL },

    { BW_MOD_REGION_OVL(21), 0x021C3530u, BW_EVENT_MON_EXP, BW_EVENT_BEFORE,
      BW_SCOPE_ANY, 0u, 0u, build_mon_exp_daycare, apply_mon_exp_daycare, NULL },

    { BW_MOD_REGION_OVL(93), 0x021D69B8u, BW_EVENT_MON_LEVELUP, BW_EVENT_AFTER,
      BW_SCOPE_INSIDE, BW_MOD_REGION_OVL(93), 0x021C5350u,
      build_mon_levelup, NULL, NULL },

    { BW_MOD_REGION_ARM9, 0x02018328u, BW_EVENT_MOVE_LEARN, BW_EVENT_BEFORE,
      BW_SCOPE_ANY, 0u, 0u, build_move_learn_level, NULL,
      refuse_move_learn_level },

    { BW_MOD_REGION_OVL(21), 0x021C360Cu, BW_EVENT_MOVE_LEARN, BW_EVENT_BEFORE,
      BW_SCOPE_ANY, 0u, 0u, build_move_learn_daycare, NULL,
      refuse_move_learn_daycare },

    { BW_MOD_REGION_OVL(170), 0x021DD314u, BW_EVENT_MON_RELEASE, BW_EVENT_BEFORE,
      BW_SCOPE_OUTSIDE, BW_MOD_REGION_OVL(170), 0x021DD530u,
      build_mon_release, NULL, refuse_mon_release },

    { BW_MOD_REGION_OVL(170), 0x021DD530u, BW_EVENT_BOX_DEPOSIT, BW_EVENT_BEFORE,
      BW_SCOPE_ANY, 0u, 0u, build_box_deposit, NULL, refuse_box_move },

    { BW_MOD_REGION_OVL(170), 0x021DD530u, BW_EVENT_BOX_WITHDRAW,
      BW_EVENT_BEFORE, BW_SCOPE_ANY, 0u, 0u, build_box_withdraw, NULL,
      refuse_box_move },

    { BW_MOD_REGION_ARM9, 0x0201AB8Cu, BW_EVENT_PARTY_SWAP, BW_EVENT_BEFORE,
      BW_SCOPE_OUTSIDE, BW_MOD_REGION_OVL(170), 0x021DD530u,
      build_party_swap, NULL, refuse_party_swap },

    { BW_MOD_REGION_OVL(91), 0x021BA624u, BW_EVENT_MON_HELDITEM, BW_EVENT_BEFORE,
      BW_SCOPE_ANY, 0u, 0u, build_mon_helditem, NULL, NULL },

    { BW_MOD_REGION_OVL(21), 0x021B63D4u, BW_EVENT_EGG_HATCH, BW_EVENT_BEFORE,
      BW_SCOPE_ANY, 0u, 0u, build_egg_hatch, NULL, NULL },

    { BW_MOD_REGION_OVL(21), 0x021B654Cu, BW_EVENT_TRADE_INGAME, BW_EVENT_BEFORE,
      BW_SCOPE_ANY, 0u, 0u, build_trade_ingame, NULL, refuse_trade_ingame }
};

/* The rows of this area, for the dispatch core. */
const BwEventBinding *bw_event_bindings_mon(u32 *count)
{
    if (count != NULL) {
        *count = (u32)(sizeof(g_mon_rows) / sizeof(g_mon_rows[0]));
    }
    return g_mon_rows;
}
