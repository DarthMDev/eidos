/*
 * sdk/mod_event.h -- the port's event dispatch (docs/mod-implementation.md,
 * section 1).
 *
 * A slot whose declared row carries a stable event calls into this layer
 * around the game's function: bw_mod_event_enter before, bw_mod_event_leave
 * after.  The layer builds the event's payload from the call, runs every
 * handler a mod has bound to the event, and writes the fields a handler may
 * modify back into the call.
 *
 * This header is the port's own; a mod never sees it.  What a mod sees is
 * sdk/mod_api.h (the events and their payloads) and sdk/mod_state.h (the
 * state and service calls).
 */

#ifndef BW_PORT_MOD_EVENT_H
#define BW_PORT_MOD_EVENT_H

#include "types.h"
#include "mod_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* The call                                                            */
/* ------------------------------------------------------------------ */

/*
 * How many arguments a bound game function can take.  The DS's own are few;
 * the widest the declared set binds is under this.
 */
#define BW_EVENT_ARGS_MAX 8

/*
 * The call a slot hands to the event layer: the arguments the game function
 * takes and the value it returned.
 *
 * The arguments are held as words, not as the game's types, so the layer can
 * carry a call whose prototype it does not know.  The slot writes them back
 * with the function's own casts, so a handler's change to an argument
 * reaches the game.
 */
typedef struct BwEventCall {
    uintptr_t a[BW_EVENT_ARGS_MAX];
    u32  nargs;
    s64  ret;
} BwEventCall;

/* ------------------------------------------------------------------ */
/* The payload                                                         */
/* ------------------------------------------------------------------ */

/*
 * The payload the port builds on the slot's stack.  It is the event's own
 * struct (docs/mod-payloads.md, section 2), so a handler reads the fields
 * its event defines.  The union carries every payload, and `bytes` gives the
 * layer one buffer to build any of them in.
 */
#define BW_EVENT_PAYLOAD_MAX 128

typedef union BwEventPayload {
    BwEvent h;

    BwGameNew        game_new;
    BwSaveBegin      save_begin;
    BwSaveDone       save_done;
    BwSaveLoad       save_load;
    BwGameCleared    game_cleared;

    BwRouteChange    route_change;
    BwMapWarp        map_warp;
    BwPlayerStep     player_step;
    BwTrainerSpotted trainer_spotted;
    BwNpcTalk        npc_talk;
    BwMenuOpen       menu_open;
    BwMenuBuild      menu_build;
    BwFieldMove      field_move;
    BwPcOpen         pc_open;

    BwEncounterWild    encounter_wild;
    BwEncounterStatic  encounter_static;
    BwEncounterTrainer encounter_trainer;

    BwMonCreate    mon_create;
    BwMonReceive   mon_receive;
    BwMonNickname  mon_nickname;
    BwMonEvolve    mon_evolve;
    BwMonExp       mon_exp;
    BwMonLevelup   mon_levelup;
    BwMoveLearn    move_learn;
    BwMonRelease   mon_release;
    BwBoxDeposit   box_deposit;
    BwBoxWithdraw  box_withdraw;
    BwPartySwap    party_swap;
    BwMonHelditem  mon_helditem;
    BwEggHatch     egg_hatch;
    BwTradeIngame  trade_ingame;

    BwBattleStart  battle_start;
    BwBattleAction battle_action;
    BwBattleSwitch battle_switch;
    BwBattleEscape battle_escape;
    BwBattleMove   battle_move;
    BwBattleDamage battle_damage;
    BwBattleFaint  battle_faint;
    BwBattleItem   battle_item;
    BwCatchAttempt catch_attempt;
    BwCatchResult  catch_result;
    BwBattleEnd    battle_end;
    BwWhiteout     whiteout;

    BwItemAdd     item_add;
    BwItemUse     item_use;
    BwShopBuy     shop_buy;
    BwMoneyChange money_change;
    BwHeal        heal;

    BwBadgeGet    badge_get;
    BwFlagSet     flag_set;
    BwDexRegister dex_register;

    u8 bytes[BW_EVENT_PAYLOAD_MAX];
} BwEventPayload;

/* ------------------------------------------------------------------ */
/* The binding table                                                   */
/* ------------------------------------------------------------------ */

/* When the event fires relative to the game function. */
enum {
    BW_EVENT_BEFORE = 0,   /* the game function has not run yet */
    BW_EVENT_AFTER  = 1    /* the game function has returned */
};

/*
 * The scope of a binding: some functions only mean the event while a
 * particular caller runs them (docs/mod-events.md, section 1).
 */
enum {
    BW_SCOPE_ANY     = 0,  /* the event fires wherever the function runs */
    BW_SCOPE_INSIDE  = 1,  /* only while the scope function runs */
    BW_SCOPE_OUTSIDE = 2   /* only while the scope function does not run */
};

/* Fills the payload from the call. */
typedef void (*BwEventBuild)(const BwEventCall *call, BwEventPayload *out);

/* Writes the fields a handler modified back into the call. */
typedef void (*BwEventApply)(const BwEventPayload *in, BwEventCall *call);

/* Produces the refused outcome the event names (docs/mod-events.md). */
typedef void (*BwEventRefuse)(BwEventCall *call);

/*
 * One row: the (region, address) pair the event fires from, the event it
 * is, and what the layer does with it.
 */
typedef struct BwEventBinding {
    unsigned int region;      /* BW_MOD_REGION_ARM9 or BW_MOD_REGION_OVL(id) */
    unsigned int addr;        /* the even function start */
    u32           kind;       /* the BW_EVENT_* id */
    u8            point;      /* BW_EVENT_BEFORE or BW_EVENT_AFTER */
    u8            scope;      /* BW_SCOPE_* */
    unsigned int scope_region;/* the region of the scope function */
    unsigned int scope_addr;  /* the address of the scope function */
    BwEventBuild  build;      /* fills the payload from the call */
    BwEventApply  apply;      /* writes the modified fields back, or NULL */
    BwEventRefuse refuse;     /* produces the refused outcome, or NULL */
} BwEventBinding;

/* ------------------------------------------------------------------ */
/* The dispatch                                                        */
/* ------------------------------------------------------------------ */

/*
 * The event fires before the game function runs.  `payload` is the port's
 * buffer: the layer fills it, runs the handlers, and applies what they
 * changed to `call`.
 *
 * Returns 1 when a handler refused the event, in which case the slot does
 * not call the game function and returns `call.ret` (the refusal function
 * has set it).  Returns 0 when the event goes on.
 */
int bw_mod_event_enter(unsigned int region, unsigned int addr,
                       BwEventCall *call, BwEventPayload *payload);

/*
 * The event fires after the game function ran: `call.ret` holds its result,
 * and the payload carries it.  The layer applies what the handlers changed
 * to `call` as well, so a handler can change the result the caller sees.
 */
void bw_mod_event_leave(unsigned int region, unsigned int addr,
                        BwEventCall *call, BwEventPayload *payload);

/*
 * The scope stack.  Every slot pushes its (region, address) on entry and
 * pops it on return, so a binding that is scoped to a function can tell
 * whether that function is running (docs/mod-implementation.md, section 1).
 */
void bw_mod_event_push(unsigned int region, unsigned int addr);
void bw_mod_event_pop(void);

/* Whether the scope function of the binding at the top of the stack runs. */
int  bw_mod_event_scope_matches(unsigned int scope_region,
                                 unsigned int scope_addr, unsigned int scope);

/*
 * A mod binds a handler to an event.  `kind` is a BW_EVENT_* id.  Every
 * handler bound to an event runs, in load order (docs/mod-events.md,
 * section 3).  Returns 0, or -1 when the handler table is full.
 */
int bw_mod_event_register(u32 kind, BwEventHandler fn);

/*
 * The port's own bindings: one row per stable-event binding of
 * docs/mod-events.md, section 1.  The layer reads them from the tables the
 * area files publish.
 */
int  bw_mod_event_bind(const BwEventBinding *binding);
void bw_mod_event_bind_area(const BwEventBinding *rows, u32 count);

/* Drop every handler and every binding: the state at start-up and at a
 * reset. */
void bw_mod_event_reset(void);

/* How many handlers are bound to an event, for the tests and the reports. */
u32 bw_mod_event_handler_count(u32 kind);

/* How many refusals were ignored because the event cannot be refused. */
u32 bw_mod_event_ignored_refusals(void);

#ifdef __cplusplus
}
#endif
#endif /* BW_PORT_MOD_EVENT_H */
