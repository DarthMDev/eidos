/*
 * sdk/mod_state.h -- the state and service calls a mod binds to
 * (docs/mod-abi.md, section 1; docs/mod-events.md, section 4;
 * docs/mod-services.md).
 *
 * A mod reads and writes game state through accessor functions the port
 * wraps, never through the game's structs, so the game's layout can change
 * behind them.  The port services are the things that are not game
 * functions at all: text on screen, peer sessions, a timer.
 *
 * Both tables are frozen within an interface version, like the rest of
 * sdk/mod_api.h.
 */

#ifndef BW_PORT_MOD_STATE_H
#define BW_PORT_MOD_STATE_H

#include "types.h"
#include "mod_api.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* The state calls                                                     */
/* ------------------------------------------------------------------ */

/*
 * The field ids a Pokemon's fields are read and written by.  They are the
 * ids the game's own accessors take (POKE_TOOL_GetPartyValue 0x02017E1C and
 * POKE_TOOL_SetPartyValue 0x02017E40), so a mod asks for the same values
 * the game asks for.
 */
enum {
    BW_MON_FIELD_SPECIES   = 0x05,
    BW_MON_FIELD_ITEM      = 0x06,
    BW_MON_FIELD_EXP       = 0x08,
    BW_MON_FIELD_LEVEL     = 0x9E,
    BW_MON_FIELD_HP        = 0xA0,
    BW_MON_FIELD_MAX_HP    = 0xA1,
    BW_MON_FIELD_ATK       = 0xA2,
    BW_MON_FIELD_DEF       = 0xA3,
    BW_MON_FIELD_SPEED     = 0xA4,
    BW_MON_FIELD_SPATK     = 0xA5,
    BW_MON_FIELD_SPDEF     = 0xA6,
    BW_MON_FIELD_TYPE1     = 0xAE,
    BW_MON_FIELD_TYPE2     = 0xAF
};

/*
 * The state table.  Every call returns 0 on success and a non-zero value on
 * failure, except the ones that answer with a value: they return the value
 * itself and a negative number for "no such thing".
 *
 * The rules a mod relies on (docs/mod-events.md, section 4):
 *
 * - The battle works on a copy.  A write to the party record has no effect
 *   during a battle and is overwritten when the battle writes its results
 *   back; a mod that changes a Pokemon in battle writes the battle record.
 * - The game's later write wins.  The port does not arbitrate a write.
 * - Overlay data is reloaded.  A save-block value persists; an overlay's own
 *   data does not.
 * - Moves wait for a quiet moment.  mon_to_box and mon_take_item change the
 *   party, which a battle, the party screen and the PC hold open; the port
 *   queues them and applies them when none of those is running.
 */
typedef struct BwModState {
    /* The party. */
    u32 (*party_count)(void);
    int (*party_ref)(u32 slot, BwMonRef *out);          /* a reference to a party slot */
    int (*party_get)(BwMonRef mon, u32 field, u32 *out);
    int (*party_set)(BwMonRef mon, u32 field, u32 value);

    /* The boxes. */
    u32 (*box_count)(void);
    int (*box_ref)(u32 box, u32 slot, BwMonRef *out);
    int (*box_get)(BwMonRef mon, u32 field, u32 *out);
    int (*box_set)(BwMonRef mon, u32 field, u32 value);

    /* A Pokemon in battle. */
    int (*battle_ref)(u32 side, u32 slot, BwMonRef *out);
    int (*battle_get)(BwMonRef mon, u32 field, u32 *out);
    int (*battle_set)(BwMonRef mon, u32 field, u32 value);

    /* Species data: the personal parameters of a species. */
    int (*species_value)(u16 species, u32 field, u32 *out);
    int (*species_shiny)(u32 personality, u16 species);

    /* The dex. */
    int (*dex_caught)(u16 species);

    /* Badges, money and flags. */
    u32 (*badge_count)(void);
    int (*badge_has)(u32 badge);
    u32 (*money)(void);
    int  (*money_set)(u32 amount);
    int  (*flag)(u32 flag, u32 *out);
    int  (*flag_set)(u32 flag, u32 value);

    /* The map and the player. */
    u16 (*zone)(void);
    int (*player_pos)(s16 *x, s16 *y, s16 *z);
    u8  (*battle_style)(void);

    /* Moves between places. */
    int (*mon_to_box)(BwMonRef mon);
    int (*mon_take_item)(BwMonRef mon);
} BwModState;

/* ------------------------------------------------------------------ */
/* The services                                                        */
/* ------------------------------------------------------------------ */

/* What a session handler is told about. */
enum {
    BW_SESSION_MESSAGE = 0,   /* a message from the peer */
    BW_SESSION_JOIN    = 1,   /* a peer joined */
    BW_SESSION_LEAVE   = 2    /* a peer left */
};

/* A message from a peer (the payload is the peer's own bytes). */
typedef void (*BwSessionHandler)(u32 peer, int kind, const void *buf, u32 size);

/* A text command from an external tool. */
typedef void (*BwCommandHandler)(const char *line);

/* Once per game frame, after the game's own frame work. */
typedef void (*BwFrameHandler)(void);

/* A save sent by a peer. */
typedef void (*BwSaveHandler)(u32 peer, const void *buf, u32 size);

/* The flags of a run restart. */
enum {
    BW_RESTART_NEW_GAME    = 0x1u,
    BW_RESTART_DELETE_SAVE = 0x2u
};

/*
 * The header of the port's own state file (docs/mod-state.md, section 1).
 * The port owns it and bumps `version` when the layout changes; the payload
 * after it belongs to the mod.
 */
typedef struct BwModStateHeader {
    u32 magic;         /* BWMS */
    u16 version;       /* the port's header version */
    u16 flags;         /* zero */
    u32 payload_size;  /* the bytes after the header */
    u32 payload_crc;   /* the payload's checksum */
    u32 api_written;   /* the manifest api in force */
    u32 save_check;    /* a checksum of the game save written in the same step */
} BwModStateHeader;

#define BW_MOD_STATE_MAGIC 0x534D5742u   /* "BWMS" little-endian */

/*
 * The services table.  The calls the port has not built yet are NULL, and a
 * mod that calls one gets nothing rather than a crash.
 */
typedef struct BwModServices {
    /* Messages. */
    void (*message)(const char *text);
    int  (*confirm)(const char *text);

    /* Peer sessions. */
    int  (*session_host)(char *code_out, u32 size);
    int  (*session_join)(const char *code);
    void (*session_leave)(void);
    int  (*session_send)(u32 peer, const void *buf, u32 size);
    void (*on_session)(BwSessionHandler fn);

    /* Field avatars. */
    u32  (*avatar_create)(u32 sprite, s16 x, s16 z, u8 dir);
    void (*avatar_move)(u32 avatar, s16 x, s16 y, s16 z, u8 dir, u8 mode);
    void (*avatar_delete)(u32 avatar);

    /* Pokemon export and import. */
    int (*mon_export)(BwMonRef mon, void *buf, u32 size);
    int (*mon_import)(const void *buf, u32 size, BwMonRef *out);

    /* The run-status panel. */
    void (*panel_set)(u32 line, const char *text);
    void (*panel_clear)(void);

    /* The timer and the splits. */
    void (*timer_start)(void);
    void (*timer_stop)(void);
    u64  (*timer_ms)(void);
    void (*split)(const char *name);

    /* A run restart. */
    void (*run_restart)(u32 flags);

    /* The mod's own random source. */
    void (*rand_seed)(u64 seed);
    u32  (*rand_next)(void);

    /* External control. */
    void (*on_command)(BwCommandHandler fn);
    void (*publish)(const char *topic, const char *json);

    /* Input injection. */
    void (*input_press)(u32 buttons, u32 frames);

    /* The frame tick. */
    void (*on_frame)(BwFrameHandler fn);

    /* The save handoff. */
    int  (*save_send)(u32 peer);
    void (*on_save_received)(BwSaveHandler fn);

    /* The mod's own settings. */
    int  (*setting_get)(const char *key);

    /* The mod's own saved data (docs/mod-state.md, section 2). */
    long (*state_load)(void *buf, u32 size);
    int  (*state_store)(const void *buf, u32 size);
    int  (*state_clear)(void);
    int  (*state_info)(BwModStateHeader *out);
    int  (*state_dir)(char *out, u32 out_size);
} BwModServices;

#ifdef __cplusplus
}
#endif
#endif /* BW_PORT_MOD_STATE_H */
