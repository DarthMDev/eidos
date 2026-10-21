/*
 * sdk/mod_api.h -- the mod ABI: the types, the events and the entry points
 * (docs/mod-abi.md).
 *
 * A mod with code is a native library compiled against this header and
 * sdk/mod_state.h.  Everything a mod compiles against is in those two
 * headers, and nothing else is part of the ABI.  The surface is frozen
 * within an interface version: event names, event ids, payload layouts, the
 * handler type and every state and service call stay as they are, and a
 * change to any of them raises BW_MOD_API_VERSION and the surface hash
 * (docs/mod-abi.md, section 3).
 *
 * The port owns every struct here.  A payload is filled by the port when the
 * event fires; the game's own structs are never exposed, because aletheia
 * restructures them (docs/mod-payloads.md, section 0).
 */

#ifndef BW_PORT_MOD_API_H
#define BW_PORT_MOD_API_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The interface version.  A mod states the version it was built against in
 * its manifest, and the port refuses a mod whose number is outside
 * BW_MOD_API_MIN to BW_MOD_API_VERSION (the rule in docs/mod-abi.md,
 * section 3).
 *
 * Version 2 added the script command hooks: the host table's script call
 * and the command's view.
 *
 * Version 3 gave the command's view the whole operand area: the operands
 * are bytes and the view holds every byte of every command of both script
 * VMs, so a handler can reach an operand past the eighth word.  It added
 * the services table's state_in_step, which answers for the calling mod
 * whether its own saved data was written with the save in force.
 *
 * Version 4 appended the state table's mon_release, item_count and
 * item_add.  Nothing moved, so a
 * version 3 mod still loads.
 */
#define BW_MOD_API_VERSION 4
/* The oldest interface a mod may state: version 3 mods see the same table
 * without the entries version 4 appended. */
#define BW_MOD_API_MIN 3

/*
 * The surface hash: a digest of every struct's size and field offsets,
 * every enum value and every table member in this header and in
 * sdk/mod_state.h.  A test recomputes it and compares; a change to the
 * surface that does not raise BW_MOD_API_VERSION and update this number
 * fails the test (docs/mod-abi.md, section 3).
 *
 * This is the 32-bit number, the port's reference configuration.  The three
 * tables hold function pointers, so their layout follows the pointer width
 * and the hash follows it; the test carries the other width's number, so a
 * change fails on every width the port is built at.
 */
#define BW_MOD_API_HASH 0x29D5958Du

/* ------------------------------------------------------------------ */
/* The event header                                                    */
/* ------------------------------------------------------------------ */

/*
 * Every payload starts with this.  A handler reads `kind` to know the event
 * and `size` to know how much of the payload the port filled; a payload only
 * grows at the end within a version.
 */
typedef struct BwEvent {
    u32 size;   /* sizeof the concrete payload, as the port built it */
    u32 kind;   /* the BW_EVENT_* id of the event */
} BwEvent;

/* ------------------------------------------------------------------ */
/* The Pokemon reference                                                */
/* ------------------------------------------------------------------ */

/* Where a Pokemon is. */
enum {
    BW_MON_PARTY  = 0,
    BW_MON_BOX    = 1,
    BW_MON_BATTLE = 2,
    BW_MON_WILD   = 3
};

/*
 * A Pokemon, as a payload names it.  `handle` is the game's own reference (a
 * party or box record, or a battle record) and is only ever passed back to
 * the state calls, so the game's record layout stays out of the ABI.
 */
typedef struct BwMonRef {
    u32 handle;   /* opaque; passed back to the state calls, never read */
    u8  place;    /* BW_MON_PARTY, BW_MON_BOX, BW_MON_BATTLE, BW_MON_WILD */
    u8  side;     /* in battle: 0 the player's side, 1 the opponent's */
    u8  box;      /* in a box: the box number */
    u8  slot;     /* the position in the party, the box or the battle side */
} BwMonRef;

/* An empty reference: no Pokemon. */
#define BW_MON_REF_NONE { 0u, (u8)BW_MON_PARTY, 0u, 0u, 0u }

/* ------------------------------------------------------------------ */
/* The handler and its return                                          */
/* ------------------------------------------------------------------ */

/*
 * A handler is one C function per event.  `mod_state` is the mod's own
 * pointer (the value its start-up entry returned) and `event` points at the
 * payload, valid only for the call (docs/mod-events.md, section 2).
 */
typedef int (*BwEventHandler)(void *mod_state, BwEvent *event);

enum {
    BW_EVENT_CONTINUE = 0,   /* the game goes on */
    BW_EVENT_REFUSE   = 1    /* the port produces the event's refused outcome */
};

/* ------------------------------------------------------------------ */
/* The event ids                                                       */
/* ------------------------------------------------------------------ */

/*
 * One constant per stable event, named after the event in upper case:
 * `battle.faint` is BW_EVENT_BATTLE_FAINT.  An id never changes and is
 * never reused.  The list is the one of docs/mod-events.md, section 1, and
 * the bindings are mod/declared_set.txt's fourth column.
 */
enum {
    /* session */
    BW_EVENT_GAME_NEW    = 1,
    BW_EVENT_SAVE_BEGIN  = 2,
    BW_EVENT_SAVE_DONE   = 3,
    BW_EVENT_SAVE_LOAD   = 4,
    BW_EVENT_GAME_CLEARED = 5,

    /* world */
    BW_EVENT_ROUTE_CHANGE = 10,
    BW_EVENT_MAP_WARP     = 11,
    BW_EVENT_PLAYER_STEP  = 12,
    BW_EVENT_TRAINER_SPOTTED = 13,
    BW_EVENT_NPC_TALK     = 14,
    BW_EVENT_MENU_OPEN    = 15,
    BW_EVENT_MENU_BUILD   = 16,
    BW_EVENT_FIELD_MOVE   = 17,
    BW_EVENT_PC_OPEN      = 18,

    /* encounters */
    BW_EVENT_ENCOUNTER_WILD     = 30,
    BW_EVENT_ENCOUNTER_STATIC   = 31,
    BW_EVENT_ENCOUNTER_TRAINER  = 32,

    /* pokemon */
    BW_EVENT_MON_CREATE    = 40,
    BW_EVENT_MON_RECEIVE   = 41,
    BW_EVENT_MON_NICKNAME  = 42,
    BW_EVENT_MON_EVOLVE    = 43,
    BW_EVENT_MON_EXP       = 44,
    BW_EVENT_MON_LEVELUP   = 45,
    BW_EVENT_MOVE_LEARN    = 46,
    BW_EVENT_MON_RELEASE   = 47,
    BW_EVENT_BOX_DEPOSIT   = 48,
    BW_EVENT_BOX_WITHDRAW  = 49,
    BW_EVENT_PARTY_SWAP    = 50,
    BW_EVENT_MON_HELDITEM  = 51,
    BW_EVENT_EGG_HATCH     = 52,
    BW_EVENT_TRADE_INGAME  = 53,

    /* battle */
    BW_EVENT_BATTLE_START  = 70,
    BW_EVENT_BATTLE_ACTION = 71,
    BW_EVENT_BATTLE_ITEM   = 72,
    BW_EVENT_BATTLE_SWITCH = 73,
    BW_EVENT_BATTLE_ESCAPE = 74,
    BW_EVENT_BATTLE_MOVE   = 75,
    BW_EVENT_BATTLE_DAMAGE = 76,
    BW_EVENT_BATTLE_FAINT  = 77,
    BW_EVENT_CATCH_ATTEMPT = 78,
    BW_EVENT_CATCH_RESULT  = 79,
    BW_EVENT_BATTLE_END    = 80,
    BW_EVENT_WHITEOUT      = 81,
    BW_EVENT_BATTLE_TRAINER_MON = 82,   /* version 4 */

    /* items, money and healing */
    BW_EVENT_ITEM_ADD     = 90,
    BW_EVENT_ITEM_USE     = 91,
    BW_EVENT_SHOP_BUY     = 92,
    BW_EVENT_MONEY_CHANGE = 93,
    BW_EVENT_HEAL         = 94,
    BW_EVENT_HEAL_HP      = 95,     /* version 4 */

    /* progress */
    BW_EVENT_BADGE_GET    = 110,
    BW_EVENT_FLAG_SET     = 111,
    BW_EVENT_DEX_REGISTER = 112
};

/* ------------------------------------------------------------------ */
/* The small enums                                                     */
/* ------------------------------------------------------------------ */

/* How an encounter was rolled (encounter.wild). */
enum {
    BW_ENCOUNTER_GRASS       = 0,
    BW_ENCOUNTER_DARK_GRASS   = 1,
    BW_ENCOUNTER_SURF        = 2,
    BW_ENCOUNTER_FISHING     = 3,
    BW_ENCOUNTER_SHAKING     = 4,
    BW_ENCOUNTER_RIPPLING    = 5,
    BW_ENCOUNTER_DUST        = 6
};

/* How a Pokemon reached the player (mon.receive). */
enum {
    BW_RECEIVE_CATCH      = 0,
    BW_RECEIVE_GIFT       = 1,
    BW_RECEIVE_STATIC     = 2,
    BW_RECEIVE_EGG        = 3,
    BW_RECEIVE_TRADE      = 4
};

/* The battle format (battle.start, encounter.trainer). */
enum {
    BW_FORMAT_SINGLE   = 0,
    BW_FORMAT_DOUBLE   = 1,
    BW_FORMAT_TRIPLE   = 2,
    BW_FORMAT_ROTATION = 3,
    BW_FORMAT_MULTI    = 4
};

/* What the player chose in battle (battle.action). */
enum {
    BW_ACTION_FIGHT  = 0,
    BW_ACTION_SWITCH = 1,
    BW_ACTION_RUN    = 2
};

/* How a battle ended (battle.end). */
enum {
    BW_OUTCOME_WIN    = 0,
    BW_OUTCOME_LOSS   = 1,
    BW_OUTCOME_FLED   = 2,
    BW_OUTCOME_CAUGHT = 3,
    BW_OUTCOME_DRAW   = 4
};

/* How a Pokemon learns a move (move.learn). */
enum {
    BW_MOVE_SOURCE_LEVEL      = 0,
    BW_MOVE_SOURCE_TM         = 1,
    BW_MOVE_SOURCE_TUTOR      = 2,
    BW_MOVE_SOURCE_RELEARNER = 3
};

/* How a Pokemon evolves (mon.evolve). */
enum {
    BW_EVOLVE_LEVEL      = 0,
    BW_EVOLVE_ITEM       = 1,
    BW_EVOLVE_TRADE      = 2,
    BW_EVOLVE_FRIENDSHIP = 3,
    BW_EVOLVE_OTHER      = 4
};

/* Where a heal came from (heal). */
enum {
    BW_HEAL_CENTRE = 0,
    BW_HEAL_SCRIPT = 1,
    BW_HEAL_POWER  = 2
};

/* Where experience comes from (mon.exp). */
enum {
    BW_EXP_BATTLE  = 0,
    BW_EXP_DAYCARE = 1
};

/* Where an HP restore happens (heal.hp). */
enum {
    BW_HEAL_HP_FIELD  = 0,  /* an item used from the bag or the party screen */
    BW_HEAL_HP_BATTLE = 1   /* any HP restore in a battle: a bag item, a held
                             * item, a move or an ability */
};

/* ------------------------------------------------------------------ */
/* The payloads                                                        */
/* ------------------------------------------------------------------ */

/*
 * One struct per stable event, BwEvent first (docs/mod-payloads.md,
 * section 2).  The fields marked *mod* in that document may be written by a
 * handler; the port reads them back and applies them before the game
 * function continues.  A write to any other field has no effect.
 */

/* session */

typedef struct BwGameNew {
    BwEvent h;
} BwGameNew;

typedef struct BwSaveBegin {
    BwEvent h;
    u32 play_seconds;   /* play time */
    u16 zone;           /* where the player saves */
} BwSaveBegin;

typedef struct BwSaveDone {
    BwEvent h;
    u32 play_seconds;
    u16 zone;
    u8  ok;             /* the save was written */
} BwSaveDone;

typedef struct BwSaveLoad {
    BwEvent h;
    u32 play_seconds;
    u16 zone;
    u8  state_in_step;  /* the port's own saved data; the port does not
                         * fill it, and a mod's own file is per mod, so the
                         * mod asks the services table's state_in_step */
} BwSaveLoad;

typedef struct BwGameCleared {
    BwEvent h;
    u32 play_seconds;
    u8  team_count;
    BwMonRef team[6];
} BwGameCleared;

/* world */

typedef struct BwRouteChange {
    BwEvent h;
    u16 zone;       /* the new zone */
    u16 prev_zone;  /* the zone left */
    u8  season;
} BwRouteChange;

typedef struct BwMapWarp {
    BwEvent h;
    u16 zone;       /* where the warp starts */
    u16 dest_zone;  /* *mod: where it leads */
    s16 dest_x;     /* *mod: the arrival cell */
    s16 dest_z;     /* *mod */
} BwMapWarp;

typedef struct BwPlayerStep {
    BwEvent h;
    u16 zone;
    s16 x, y, z;    /* the cell after the step */
    u8  dir;        /* the facing */
    u8  mode;       /* walking, running, cycling, surfing */
} BwPlayerStep;

typedef struct BwTrainerSpotted {
    BwEvent h;
    u16 trainer_id;
    u16 partner_id; /* the second trainer of a double, or 0 */
    u8  distance;   /* cells between them */
} BwTrainerSpotted;

typedef struct BwNpcTalk {
    BwEvent h;
    u16 zone;
    u16 object_id;  /* the person or object talked to */
    u16 script_id;  /* the script the talk runs */
} BwNpcTalk;

typedef struct BwMenuOpen {
    BwEvent h;
} BwMenuOpen;

typedef struct BwMenuBuild {
    BwEvent h;
    u32 entries;    /* *mod: one bit per menu entry shown */
} BwMenuBuild;

typedef struct BwFieldMove {
    BwEvent h;
    u16 move;       /* the field move */
    BwMonRef user;  /* the Pokemon using it */
} BwFieldMove;

typedef struct BwPcOpen {
    BwEvent h;
} BwPcOpen;

/* encounters */

typedef struct BwEncounterWild {
    BwEvent h;
    u16 zone;
    u8  method;     /* BW_ENCOUNTER_* */
    u8  slot;       /* the encounter table slot rolled */
    u16 species;    /* *mod */
    u8  level;      /* *mod */
    u8  shiny;      /* *mod: 1 forces shiny, 0 leaves the roll */
    u8  double_;    /* a double wild battle */
    /* Version 4. */
    u8  reroll;     /* *mod: 1 rolls the encounter again and fires again */
    u8  level2;     /* *mod: the second Pokemon of a double (dark grass) */
    u16 species2;   /* *mod: the same; 0 when there is no second */
} BwEncounterWild;

typedef struct BwEncounterStatic {
    BwEvent h;
    u16 zone;
    u16 species;    /* *mod */
    u8  level;      /* *mod */
    u8  kind;       /* static, legendary, scripted */
} BwEncounterStatic;

typedef struct BwEncounterTrainer {
    BwEvent h;
    u16 trainer_id;
    u16 partner_id; /* the second trainer, or 0 */
    u8  format;     /* BW_FORMAT_* */
} BwEncounterTrainer;

/* pokemon */

typedef struct BwMonCreate {
    BwEvent h;
    u16 species;    /* *mod */
    u8  level;      /* *mod */
    u8  shiny;      /* *mod */
    u8  nature;     /* *mod: the nature, or 0xFF to leave the roll */
    u32 personality; /* the personality value the game rolled */
} BwMonCreate;

typedef struct BwMonReceive {
    BwEvent h;
    BwMonRef mon;
    u8  source;     /* BW_RECEIVE_* */
    u8  to_box;     /* it goes to a box because the party is full */
    u16 species;
    u8  level;
    u8  shiny;
    u16 zone;
} BwMonReceive;

typedef struct BwMonNickname {
    BwEvent h;
    BwMonRef mon;
    u8  force;      /* *mod: 1 opens the prompt even when the game would skip it */
} BwMonNickname;

typedef struct BwMonEvolve {
    BwEvent h;
    BwMonRef mon;
    u16 from;       /* its species */
    u16 to;         /* *mod: the species it evolves into */
    u8  method;     /* BW_EVOLVE_* */
} BwMonEvolve;

typedef struct BwMonExp {
    BwEvent h;
    BwMonRef mon;
    u32 amount;     /* *mod: the experience to add */
    u32 exp;        /* its experience before */
    u8  level;      /* its level before */
    /* Version 4. */
    u8  source;     /* BW_EXP_* */
    u8  wild;       /* a battle source: 1 the battle is a wild one */
} BwMonExp;

typedef struct BwMonLevelup {
    BwEvent h;
    BwMonRef mon;
    u8  from;       /* the level before */
    u8  to;         /* the level reached */
} BwMonLevelup;

typedef struct BwMoveLearn {
    BwEvent h;
    BwMonRef mon;
    u16 move;
    u8  source;     /* BW_MOVE_SOURCE_* */
    u8  slot;       /* the move slot it goes into, or 0xFF for a free one */
    u16 replaced;   /* the move it replaces, or 0 */
} BwMoveLearn;

typedef struct BwMonRelease {
    BwEvent h;
    BwMonRef mon;
    u16 species;
} BwMonRelease;

typedef struct BwBoxDeposit {
    BwEvent h;
    BwMonRef mon;   /* the Pokemon, where it is now */
    u8  box;        /* the box it moves into or out of */
    u8  slot;       /* the box slot */
} BwBoxDeposit;

typedef struct BwBoxWithdraw {
    BwEvent h;
    BwMonRef mon;
    u8  box;
    u8  slot;
} BwBoxWithdraw;

typedef struct BwPartySwap {
    BwEvent h;
    u8  slot_a, slot_b;  /* the two party positions */
} BwPartySwap;

typedef struct BwMonHelditem {
    BwEvent h;
    BwMonRef mon;
    u16 item;       /* the item given */
    u16 previous;   /* the item it held, or 0 */
} BwMonHelditem;

typedef struct BwEggHatch {
    BwEvent h;
    BwMonRef mon;
    u16 species;
} BwEggHatch;

typedef struct BwTradeIngame {
    BwEvent h;
    u16 trade_id;
    BwMonRef given;        /* the player's Pokemon */
    u16 received_species;  /* the species offered */
} BwTradeIngame;

/* battle */

typedef struct BwBattleStart {
    BwEvent h;
    u8  wild;
    u8  format;     /* *mod: BW_FORMAT_* */
    u16 trainer_id[2];  /* the opposing trainers, or 0 */
    s8  level_adjust;   /* *mod: added to every opposing Pokemon's level */
    u32 opponent_party; /* a handle to the opposing party */
} BwBattleStart;

typedef struct BwBattleAction {
    BwEvent h;
    BwMonRef mon;   /* the Pokemon choosing */
    u8  side;       /* 0 the player's, 1 the opponent's */
    u8  action;     /* BW_ACTION_* */
    u16 move;       /* for fight: the move checked */
    u8  switch_to;  /* for switch: the party position */
} BwBattleAction;

typedef struct BwBattleSwitch {
    BwEvent h;
    BwMonRef out;   /* the Pokemon leaving, or empty after a faint */
    BwMonRef in;    /* the Pokemon sent out */
    u8  forced;     /* the switch follows a faint or a move that forces it */
} BwBattleSwitch;

typedef struct BwBattleEscape {
    BwEvent h;
    BwMonRef mon;   /* the Pokemon trying to flee */
} BwBattleEscape;

typedef struct BwBattleMove {
    BwEvent h;
    BwMonRef user;
    u16 move;
    BwMonRef target;  /* the first target */
    u16 turn;
} BwBattleMove;

typedef struct BwBattleDamage {
    BwEvent h;
    BwMonRef attacker;
    BwMonRef target;
    u16 move;
    u32 amount;      /* *mod: the damage */
    u16 target_hp;   /* the target's HP before the hit */
    u16 target_max_hp;
    u8  critical;
    u8  effectiveness;
} BwBattleDamage;

typedef struct BwBattleFaint {
    BwEvent h;
    BwMonRef mon;       /* the Pokemon that fainted */
    u8  side;           /* 0 the player's, 1 the opponent's */
    BwMonRef cause;     /* the last attacker, or empty */
    u16 cause_move;     /* the last move, or 0 */
    u32 cause_damage;   /* the last damage, or 0 */
    u16 turn;
} BwBattleFaint;

typedef struct BwBattleItem {
    BwEvent h;
    u16 item;       /* the item chosen from the bag */
    BwMonRef mon;   /* the Pokemon whose turn it is */
    u8  ball;       /* the item is a ball */
    u8  revive;     /* the item revives a fainted Pokemon */
} BwBattleItem;

typedef struct BwCatchAttempt {
    BwEvent h;
    BwMonRef target;
    u16 species;
    u8  level;
    u8  shiny;
    u16 ball;
    u8  rate;       /* *mod: the catch rate the roll uses */
} BwCatchAttempt;

typedef struct BwCatchResult {
    BwEvent h;
    BwMonRef target;
    u16 species;
    u16 ball;
    u8  caught;     /* the catch succeeded */
    u8  shakes;     /* the shakes shown */
} BwCatchResult;

typedef struct BwBattleEnd {
    BwEvent h;
    u8  outcome;    /* BW_OUTCOME_* */
    u8  wild;
    u16 trainer_id[2];
    u16 turns;
} BwBattleEnd;

typedef struct BwWhiteout {
    BwEvent h;
    u8  in_battle;  /* 1 the battle half, 0 the field half */
    u32 money_lost;
} BwWhiteout;

/*
 * battle.trainer_mon (version 4): one Pokemon of the opposing trainer's
 * party, after the game built the party and before the battle starts.  It
 * fires once per member, in party order.  A changed field is written to
 * the Pokemon with the game's own setters: a new species or level works out
 * the stats again, a new move gets its full PP.  0 in a *mod field leaves
 * the game's value.
 */
typedef struct BwBattleTrainerMon {
    BwEvent h;
    u16 trainer_id; /* the trainer the party belongs to */
    u8  index;      /* the position in the party, 0 first */
    u8  level;      /* *mod */
    u16 species;    /* *mod */
    u16 item;       /* *mod: the held item */
    u16 move1;      /* *mod */
    u16 move2;      /* *mod */
    u16 move3;      /* *mod */
    u16 move4;      /* *mod */
    u16 ability;    /* *mod */
    u8  form;       /* *mod: the form, written when species is written */
} BwBattleTrainerMon;

/* items, money and healing */

typedef struct BwItemAdd {
    BwEvent h;
    u16 item;
    u16 count;      /* *mod: how many */
    u8  pocket;     /* the bag pocket */
} BwItemAdd;

typedef struct BwItemUse {
    BwEvent h;
    u16 item;
    BwMonRef target;  /* the Pokemon it is used on, or empty */
} BwItemUse;

typedef struct BwShopBuy {
    BwEvent h;
    u16 item;
    u32 price;      /* *mod: the price of one */
} BwShopBuy;

typedef struct BwMoneyChange {
    BwEvent h;
    s32 delta;      /* *mod: the change, positive or negative */
    u32 before;     /* the money before */
} BwMoneyChange;

typedef struct BwHeal {
    BwEvent h;
    u8  source;     /* BW_HEAL_* */
    u8  mask;       /* *mod: one bit per party position to heal */
} BwHeal;

/*
 * heal.hp (version 4): one Pokemon gets HP back.  The amount is the HP the
 * game is about to add, before the cap at max_hp; a handler changes it, or
 * refuses the event and the Pokemon gets nothing.  On the field the item is
 * still used up: a mod that wants the item kept refuses item.use instead.
 */
typedef struct BwHealHp {
    BwEvent h;
    BwMonRef mon;
    u16 hp;         /* the HP before the restore */
    u16 max_hp;
    u16 amount;     /* *mod: the HP to add */
    u8  source;     /* BW_HEAL_HP_* */
} BwHealHp;

/* progress */

typedef struct BwBadgeGet {
    BwEvent h;
    u8  badge;      /* the badge, 0 to 7 */
    u8  count;      /* badges held after it */
} BwBadgeGet;

typedef struct BwFlagSet {
    BwEvent h;
    u16 flag;       /* the event flag */
    u8  value;      /* set or cleared */
} BwFlagSet;

typedef struct BwDexRegister {
    BwEvent h;
    u16 species;
    u8  caught;     /* 1 caught, 0 seen */
} BwDexRegister;

/* ------------------------------------------------------------------ */
/* The script commands                                                 */
/* ------------------------------------------------------------------ */

/*
 * The two script VMs a mod can hook (docs/mod-events.md, section 5).  The
 * field script VM dispatches every command through one table, and the battle
 * effect VM through another; the port hooks both, so a mod can observe or
 * replace a command by its opcode without one declared row per command.
 */
enum {
    BW_SCRIPT_VM_FIELD  = 0,   /* the field script VM */
    BW_SCRIPT_VM_BATTLE = 1    /* the battle effect VM */
};

/* What a script handler answers. */
enum {
    BW_SCRIPT_CONTINUE = 0,   /* the game's own handler runs too */
    BW_SCRIPT_REPLACE  = 1    /* the game's own handler does not run */
};

/* How many operand bytes a command may carry in the view.
 *
 * The port's own width table (sdk/mod_script.c) gives the width of every
 * command of both script VMs, in bytes, and the widest of them is this
 * many: the battle effect VM's opcode 8 carries 60 bytes of operands and
 * the field VM's widest (its opcode 323) carries 22.  The view is sized
 * from that table, so no command's operand falls outside it.
 */
#define BW_SCRIPT_ARGS_MAX 60

/*
 * One command, as the port's hook sees it.
 *
 *   opcode    the command's opcode;
 *   nargs     how many operand bytes the command carries;
 *   args      the operands, as the bytes they are in the game's script.
 *             A handler writes them to change the command the game runs,
 *             and the game's own handler reads what the handlers left;
 *   pc        the program counter before the command;
 *   next_pc   the program counter after it; the hook writes it.
 *
 * The operands are bytes: the game's script holds them as bytes and the
 * game's own handler reads them off the VM's cursor a u16 or a u32 at a
 * time (eighteen of the field VM's commands carry an odd number of bytes,
 * so some commands read a
 * byte or two straight off the cursor).  The view holds them as they
 * are, so a handler reads and writes the bytes and the port writes them
 * back into the script exactly, with nothing rounded to a word.
 *
 * The view holds every byte of every command: the width table above gives
 * the width of every command of both VMs, and the widest of them is
 * BW_SCRIPT_ARGS_MAX bytes, so a handler reaches the last operand of the
 * widest command as well as the first of the narrowest.
 *
 * The program counter is a number, not a pointer: it is the hook's own
 * count of where the command sits in the script, so the view carries no
 * host pointer and reads the same at every pointer width.
 *
 * The view is valid only for the call.
 */
typedef struct BwScriptCommand {
    u16 opcode;
    u16 nargs;
    u8  args[BW_SCRIPT_ARGS_MAX];
    u32 pc;
    u32 next_pc;
} BwScriptCommand;

/*
 * The operands, read and written a word at a time.
 *
 * The index is a byte index into the operand area: the operand at 0 is the
 * first byte the command carries, and the words are little-endian, as the
 * bytes in the game's script are.  A word that reaches past the command's
 * own operands is cut short: the bytes past them read as zero and are
 * not written, so a handler cannot reach into the next command.
 *
 * These are conveniences: a handler may read and write the bytes of
 * cmd->args itself.  They are not part of the frozen surface (the hash
 * covers the structs, the enums and the tables), and they change nothing
 * a mod compiled against another version relies on.
 */
static inline u32 bw_script_arg_u16(const BwScriptCommand *cmd, u32 index)
{
    u32 limit = cmd->nargs;
    u32 v = 0;
    u32 i;

    if (limit > (u32)BW_SCRIPT_ARGS_MAX) {
        limit = (u32)BW_SCRIPT_ARGS_MAX;
    }
    for (i = 0; i < 2u; i++) {
        if (index + i >= limit) {
            break;
        }
        v |= (u32)cmd->args[index + i] << (8u * i);
    }
    return v;
}

static inline u32 bw_script_arg_u32(const BwScriptCommand *cmd, u32 index)
{
    u32 limit = cmd->nargs;
    u32 v = 0;
    u32 i;

    if (limit > (u32)BW_SCRIPT_ARGS_MAX) {
        limit = (u32)BW_SCRIPT_ARGS_MAX;
    }
    for (i = 0; i < 4u; i++) {
        if (index + i >= limit) {
            break;
        }
        v |= (u32)cmd->args[index + i] << (8u * i);
    }
    return v;
}

static inline void bw_script_set_arg_u16(BwScriptCommand *cmd, u32 index,
                                          u32 value)
{
    u32 limit = cmd->nargs;
    u32 i;

    if (limit > (u32)BW_SCRIPT_ARGS_MAX) {
        limit = (u32)BW_SCRIPT_ARGS_MAX;
    }
    for (i = 0; i < 2u; i++) {
        if (index + i >= limit) {
            break;
        }
        cmd->args[index + i] = (u8)((value >> (8u * i)) & 0xFFu);
    }
}

static inline void bw_script_set_arg_u32(BwScriptCommand *cmd, u32 index,
                                          u32 value)
{
    u32 limit = cmd->nargs;
    u32 i;

    if (limit > (u32)BW_SCRIPT_ARGS_MAX) {
        limit = (u32)BW_SCRIPT_ARGS_MAX;
    }
    for (i = 0; i < 4u; i++) {
        if (index + i >= limit) {
            break;
        }
        cmd->args[index + i] = (u8)((value >> (8u * i)) & 0xFFu);
    }
}

/*
 * A mod's handler for one command of one VM.  `mod_state` is the mod's own
 * pointer, the value its start-up entry returned, and `cmd` points at the
 * command, valid only for the call.
 */
typedef int (*BwScriptHandler)(void *mod_state, BwScriptCommand *cmd);

/* ------------------------------------------------------------------ */
/* The host table and the entry points                                 */
/* ------------------------------------------------------------------ */

/*
 * The table the port fills and hands to a mod at start-up.  It is the only
 * way into the port, so a mod links against nothing but the host's exported
 * table (docs/mod-abi.md, section 1).
 */
typedef struct BwModHost {
    u32  api;                                   /* the port's interface version */
    int  (*on)(u32 kind, BwEventHandler fn);    /* bind a handler to an event */
    int  (*script)(u32 vm, u16 opcode, BwScriptHandler fn);  /* to a command */
    const struct BwModState    *state;          /* sdk/mod_state.h */
    const struct BwModServices *services;       /* sdk/mod_state.h */
} BwModHost;

/*
 * A mod library exports these two (docs/mod-abi.md, section 1).
 *
 * eidos_mod_start runs once when the port loads the mod, after the save is
 * chosen and before the game runs.  It receives the host table and stores its
 * own state pointer; that pointer is the `mod_state` every handler receives.
 * It returns 0, or a non-zero value to tell the port to unload the mod.
 *
 * eidos_mod_stop runs when the port unloads the mod or exits.
 */
int  eidos_mod_start(const BwModHost *host, void **mod_state);
void eidos_mod_stop(void *mod_state);

#ifdef __cplusplus
}
#endif
#endif /* BW_PORT_MOD_API_H */
