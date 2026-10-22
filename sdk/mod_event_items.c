/*
 * sdk/mod_event_items.c -- the items area's event rows: the item that is
 * added to the bag, the item that is used, the shop, the money and the
 * heal (docs/mod-events.md, section 1, "Items, money and healing";
 * docs/mod-implementation.md, section 1, "The binding table"; the payloads
 * are docs/mod-payloads.md, section 2).
 *
 * One row per stable-event binding of this area.  A row names the game
 * function the event fires from (the region and the address of
 * mod/declared_set.txt), the event, the point, the scope, the builder that
 * fills the payload from the call, the applier that writes the modifiable
 * fields back and the refusal function that produces the refused outcome.
 * The dispatch core reads the rows from the table this file publishes
 * (bw_event_bindings_items) and registers them with bw_mod_event_bind_area.
 *
 * The rules the port keeps (docs/mod-payloads.md, section 0): the port owns
 * the payload; a field is filled from the call's arguments, from an accessor
 * call at the slot or from a context; a field the call cannot supply stays
 * 0 and says so in a comment; a field that points at memory the game frees
 * later is copied into the payload, never stored as a pointer.
 *
 * The game's own accessors are declared under the names src/game gives
 * them, the way sdk/mod_state.c does: the port's older address spellings
 * are redirected onto the same names by the build's gamespell header
 * (tools/gamespell.py), so one spelling links against either tree.
 */

/*
 * The overlay functions this file calls by name, for the overlay
 * link (tools/ovlink.py): each one gets its plain name, a
 * trampoline that calls the overlay resident over the address.
 *
 * symbols:
 *   SCRCMD_WORK_GetScriptId              0x0215A5D0  thumb  ov010
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
struct Party;
struct PokemonParam;
struct MiscSave;
struct MyItem;
struct BagSlot;

/* ------------------------------------------------------------------ */
/* The game's own accessors                                            */
/* ------------------------------------------------------------------ */

/*
 * The game's own functions, each with the address the corpus gives it.
 * They are weak so that a link without the game still succeeds; the port's
 * own seam, sdk/mod_state.c, does the same.
 */
#if defined(__GNUC__) || defined(__clang__)
#define BW_PORT_WEAK __attribute__((weak))
#else
#define BW_PORT_WEAK
#endif

/* The bag. */
extern u32 MYITEM_SAVEDATA_GetPocketOfItem(struct MyItem *bag, u32 itemId,
                                            struct BagSlot **slots, u32 *count,
                                            u16 heapId)
    BW_PORT_WEAK;                                        /* 0x02007D6C */

/* The misc save block: the money. */
extern u32 MISC_GetMoney(struct MiscSave *miscSave)
    BW_PORT_WEAK;                                        /* 0x0200C274 */

/* The party. */
extern void *GAME_DATA_GetMyParty(const struct GameData *gameData)
    BW_PORT_WEAK;                                        /* 0x02012934 */
extern u32 POKEPARTY_GetCount(const struct Party *party)
    BW_PORT_WEAK;                                        /* 0x0201AA18 */
extern struct PokemonParam *POKEPARTY_GetMember(struct Party *party, u32 pos)
    BW_PORT_WEAK;                                        /* 0x0201AB28 */
extern void POKE_TOOL_HealFull(struct PokemonParam *mon)
    BW_PORT_WEAK;                                        /* 0x0201B9F0 */

/* The script that runs a script command (src/game/ov010/scrcmd_work.c). */
extern u16 SCRCMD_WORK_GetScriptId(void *work) BW_PORT_WEAK;
extern u32 POKE_TOOL_GetPartyValue(struct PokemonParam *mon, u32 paramId,
                                   u32 arg) BW_PORT_WEAK;
extern void POKE_TOOL_SetPartyValue(struct PokemonParam *mon, u32 paramId,
                                    u32 value) BW_PORT_WEAK;

/* An item's own data (src/game/arm9/file_util.c), by the ids the party
 * screen reads (src/game/ov091/plist_demo_types.h). */
extern u32 FILE_UTIL_GetItemParam(u32 item, u32 sel, u32 heap) BW_PORT_WEAK;
#define BW_ITEM_PARAM_REVIVES      26u
#define BW_ITEM_PARAM_REVIVES_FULL 27u
#define BW_ITEM_PARAM_RAISES_LEVEL 28u
#define BW_ITEM_PARAM_HP_AMOUNT    58u
#define BW_ITEM_HEAP               3u   /* the game's main heap */

/* The Pokemon fields (POKE_TOOL_GetPartyValue). */
#define BW_FIELD_HP     0xA0u
#define BW_FIELD_MAX_HP 0xA1u
#define BW_FIELD_STATUS 0x9Du
#define BW_FIELD_SPECIES_ID 0x05u
#define BW_FIELD_FORM_ID    0x6Fu

/* The item ids of the five status cures (src/game/ov091/plist_demo_types.h),
 * in the order of the game's status numbers 2 sleep, 5 poison, 4 burn,
 * 3 freeze, 1 paralysis. */
#define BW_ITEM_PARAM_CURES_SLEEP     18u
#define BW_ITEM_PARAM_CURES_POISON    19u
#define BW_ITEM_PARAM_CURES_BURN      20u
#define BW_ITEM_PARAM_CURES_FREEZE    21u
#define BW_ITEM_PARAM_CURES_PARALYSIS 22u

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
 * The shop screen's work block, copied member for member from the game's
 * own header (aletheia src/game/ov021/scrcmd_proc_fld_types.h).  The game
 * hands the shop work to the bound function and to nothing else, and it
 * publishes no accessor for the item under the cursor or for its price, so
 * the port's only way to read them is a view of the block.
 *
 * The copy is what makes the read correct at either pointer width: the
 * game's ov021 code is compiled by the same host compiler as this file, so
 * the same member types in the same order give the same offsets on both.
 * The offsets the game's own header documents are the DS ones, so they are
 * checked below only where the pointers are four bytes wide.
 */
typedef struct BwPortShopItem {
    u16 itemId;
    u16 pad02;
    s32 price;
} BwPortShopItem;

typedef struct BwPortShopWork {
    u8  state;                /* 0x000 */
    u8  nextState;            /* 0x001 */
    u8  pad02[2];
    u16 heapId;               /* 0x004 */
    u8  pad06[2];
    u32 fontInfo;             /* 0x008 */
    u32 moneyData;            /* 0x00C */
    u32 bagData;              /* 0x010 */
    void *recordData;         /* 0x014 */
    u32 battlePointData;      /* 0x018 */
    void *shopTable;          /* 0x01C */
    BwPortShopItem items[40]; /* 0x020 */
    u16 itemCount;            /* 0x160 */
    u8  isTmShop;             /* 0x162 */
    u8  usesBattlePoints;     /* 0x163 */
    u32 windows[7];           /* 0x164 */
    void *itemSlots;          /* 0x180 */
    void *yesNoMenu;          /* 0x184 */
    void *shopMsgBank;        /* 0x188 */
    void *itemMsgBank;        /* 0x18C */
    void *wordSet;            /* 0x190 */
    void *priceFormat;        /* 0x194 */
    void *textBuffer;         /* 0x198 */
    void *cancelText;         /* 0x19C */
    void *soldOutText;        /* 0x1A0 */
    void *listMenu;           /* 0x1A4 */
    u32 listWindow;           /* 0x1A8 */
    u8  printerBusy;          /* 0x1AC */
    u8  pad1AD[3];
    void *printSys;           /* 0x1B0 */
    void *tcbList;            /* 0x1B4 */
    void *messagePrinter;     /* 0x1B8 */
    void *cellSystem;         /* 0x1BC */
    void *cursorActor;        /* 0x1C0 */
    void *arrowUpActor;       /* 0x1C4 */
    void *arrowDownActor;     /* 0x1C8 */
    void *quantityArrowActor; /* 0x1CC */
    void *iconActor;          /* 0x1D0 */
    u32 arrowCharRes;         /* 0x1D4 */
    u32 cursorPlttRes;        /* 0x1D8 */
    u32 cursorCellRes;        /* 0x1DC */
    u8  pad1E0[4];
    u32 arrowPlttRes;         /* 0x1E4 */
    u32 arrowCellRes;         /* 0x1E8 */
    u32 iconCharRes;          /* 0x1EC */
    u32 iconPlttRes;          /* 0x1F0 */
    u32 iconCellRes;          /* 0x1F4 */
    void *iconArchive;        /* 0x1F8 */
    u16 itemId;               /* 0x1FC: the item under the cursor */
    u8  pad1FE[2];
    u32 price;                /* 0x200 */
    u32 maxQuantity;          /* 0x204 */
    u16 quantity;             /* 0x208 */
    u16 introShown;           /* 0x20A */
} BwPortShopWork;

BW_PORT_STATIC_ASSERT(shop_item_id, sizeof(void *) != 4 ||
                      offsetof(BwPortShopWork, itemId) == 0x1FCu);
BW_PORT_STATIC_ASSERT(shop_price, sizeof(void *) != 4 ||
                      offsetof(BwPortShopWork, price) == 0x200u);
BW_PORT_STATIC_ASSERT(shop_state, sizeof(void *) != 4 ||
                      offsetof(BwPortShopWork, state) == 0x000u);
BW_PORT_STATIC_ASSERT(shop_next_state, sizeof(void *) != 4 ||
                      offsetof(BwPortShopWork, nextState) == 0x001u);
BW_PORT_STATIC_ASSERT(shop_items, sizeof(void *) != 4 ||
                      offsetof(BwPortShopWork, items) == 0x020u);
/*
 * The size of the view is the game's at either pointer width: the offsets
 * the tree's own header documents are the DS ones, and the view's members
 * are the game's own types, so the record is the same size on both.
 */
BW_PORT_STATIC_ASSERT(shop_work_size, sizeof(BwPortShopWork) ==
                      (sizeof(void *) == 4 ? 0x20Cu : 0x270u));
/*
 * The item row holds no pointer, so its size is the game's at every width.
 */
BW_PORT_STATIC_ASSERT(shop_item_size, sizeof(BwPortShopItem) == 8u);

/*
 * The script command's own work, copied member for member from the game's
 * ov010 header (aletheia src/game/ov010/scrcmd_work.c).  The heal script
 * command takes it and reads the game data out of it (SCRCMD_WORK_GetGameData
 * is `work->common->gameData`); the game publishes the work only to the
 * script VM, so the port reads it through this view.  The common work's
 * first three members are the script work, the game system and the game
 * data, and the port needs the third.
 *
 * The offsets the game's own header documents are the DS ones, so they are
 * checked below only where the pointers are four bytes wide.
 */
typedef struct BwPortScrcmdParam {
    u16   scriptId;
    u32   callParam;
    u32   callMode;
    void *scriptWork;
} BwPortScrcmdParam;

typedef struct BwPortScrcmdCommon {
    void *scriptWork;
    void *gameSystem;
    void *gameData;
} BwPortScrcmdCommon;

typedef struct BwPortScrcmdWork {
    u16                heapId;
    BwPortScrcmdParam  param;
    void              *msgData;
    u16                msgDataId;
    u8                 childScriptIndex;
    void              *savedLocalWork;
    void              *common;
} BwPortScrcmdWork;

BW_PORT_STATIC_ASSERT(scrcmd_common_game_data, sizeof(void *) != 4 ||
                      offsetof(BwPortScrcmdCommon, gameData) == 0x008u);
BW_PORT_STATIC_ASSERT(scrcmd_common_script_work, sizeof(void *) != 4 ||
                      offsetof(BwPortScrcmdCommon, scriptWork) == 0x000u);
BW_PORT_STATIC_ASSERT(scrcmd_common_size, sizeof(BwPortScrcmdCommon) ==
                      (sizeof(void *) == 4 ? 0x00Cu : 0x018u));
BW_PORT_STATIC_ASSERT(scrcmd_param_script_id, sizeof(void *) != 4 ||
                      offsetof(BwPortScrcmdParam, scriptId) == 0x000u);
BW_PORT_STATIC_ASSERT(scrcmd_param_script_work, sizeof(void *) != 4 ||
                      offsetof(BwPortScrcmdParam, scriptWork) == 0x00Cu);
BW_PORT_STATIC_ASSERT(scrcmd_param_size, sizeof(BwPortScrcmdParam) ==
                      (sizeof(void *) == 4 ? 0x010u : 0x018u));
BW_PORT_STATIC_ASSERT(scrcmd_work_msg_data, sizeof(void *) != 4 ||
                      offsetof(BwPortScrcmdWork, msgData) == 0x014u);
BW_PORT_STATIC_ASSERT(scrcmd_work_common, sizeof(void *) != 4 ||
                      offsetof(BwPortScrcmdWork, common) == 0x020u);
BW_PORT_STATIC_ASSERT(scrcmd_work_size, sizeof(BwPortScrcmdWork) ==
                      (sizeof(void *) == 4 ? 0x024u : 0x040u));

/* ------------------------------------------------------------------ */
/* The game's own constants                                            */
/* ------------------------------------------------------------------ */

/*
 * The shop's state machine (aletheia src/game/ov021/scrcmd_proc_fld_types.h).
 * The port needs one of them: the state that takes the shop back to its
 * item list.
 */
#define BW_PORT_SHOP_STATE_LIST_RESUME 4u

/* ------------------------------------------------------------------ */
/* The helpers                                                         */
/* ------------------------------------------------------------------ */

/* The party positions a heal covers: the occupied ones. */
static u8 party_mask(const struct Party *party)
{
    u32 count;

    if (party == NULL) {
        return 0u;
    }
    count = POKEPARTY_GetCount(party);
    if (count > 6u) {
        count = 6u;
    }
    return (u8)((1u << count) - 1u);
}

/* The game data of a script command, out of the command's own work. */
static void *script_game_data(uintptr_t work)
{
    const BwPortScrcmdWork *cmd = (const BwPortScrcmdWork *)work;
    const BwPortScrcmdCommon *common;

    if (cmd == NULL) {
        return NULL;
    }
    common = (const BwPortScrcmdCommon *)cmd->common;
    if (common == NULL) {
        return NULL;
    }
    return common->gameData;
}

/* The party of a script command's work. */
static struct Party *script_party(uintptr_t work)
{
    struct GameData *gameData = (struct GameData *)script_game_data(work);

    if (gameData == NULL) {
        return NULL;
    }
    return (struct Party *)GAME_DATA_GetMyParty(gameData);
}

/* ------------------------------------------------------------------ */
/* The builders                                                        */
/* ------------------------------------------------------------------ */

/*
 * item.add: an item goes into the bag.  The item and the count are the
 * arguments; the pocket is the one the game puts the item in, which the
 * game reads out of the item's own data.
 */
/* item.add: where the item comes from, read off the function the call
 * runs inside (the scope stack, as for money.change). */
static const struct {
    unsigned int region;
    unsigned int addr;
    u8 source;
} k_item_callers[] = {
    { BW_MOD_REGION_OVL(21), 0x021AF544u, (u8)BW_ITEM_FROM_SCRIPT },   /* SCRCMD_AddItem */
    { BW_MOD_REGION_OVL(21), 0x021B6FF0u, (u8)BW_ITEM_FROM_GIFT },     /* SCRCMD_POKEMON_FLD_ReceiveGiftItem */
    { BW_MOD_REGION_OVL(21), 0x021B76B4u, (u8)BW_ITEM_FROM_GIFT },     /* PDW_POSTMAN_ReceiveItems */
    { BW_MOD_REGION_OVL(21), 0x021B35A4u, (u8)BW_ITEM_FROM_PURCHASE }, /* SCRCMD_PROC_FLD_StepShop */
    { BW_MOD_REGION_OVL(18), 0x0218261Cu, (u8)BW_ITEM_FROM_PURCHASE }, /* EVENT_COMM_COMMON_ItemPurchaseEventMain */
    { BW_MOD_REGION_OVL(93), 0x021B90BCu, (u8)BW_ITEM_FROM_BATTLE },   /* BTL_MAIN_AddItemToBag */
    { BW_MOD_REGION_OVL(91), 0x021BA688u, (u8)BW_ITEM_FROM_RETURN },   /* POKELIST_ReturnBagItem */
    { BW_MOD_REGION_OVL(170), 0x021E9010u, (u8)BW_ITEM_FROM_RETURN },  /* BOX2_SEQ_StateUseItem */
    { BW_MOD_REGION_OVL(170), 0x021E91A0u, (u8)BW_ITEM_FROM_RETURN },  /* BOX2_SEQ_StateUseItemMenu */
    { BW_MOD_REGION_OVL(10), 0x0216B004u, (u8)BW_ITEM_FROM_REWARD },   /* SCRCMD_SYMBOL_ViewEventMain */
    { BW_MOD_REGION_OVL(18), 0x02180538u, (u8)BW_ITEM_FROM_REWARD }    /* EVENT_COMM_COMMON_MissionResultEventMain */
    /* The link and minigame overlays (105, 107, 136, 137) are not in the
     * port's hookable set yet; their items read BW_ITEM_FROM_OTHER. */
};

static u8 item_source(void)
{
    u32 i;

    for (i = 0; i < (u32)(sizeof(k_item_callers) / sizeof(k_item_callers[0])); i++) {
        if (bw_mod_event_scope_matches(k_item_callers[i].region,
                                       k_item_callers[i].addr, BW_SCOPE_INSIDE)) {
            return k_item_callers[i].source;
        }
    }
    return (u8)BW_ITEM_FROM_OTHER;
}

static void build_item_add(const BwEventCall *call, BwEventPayload *out)
{
    struct MyItem *bag = (struct MyItem *)call->a[0];
    u16 item = (u16)call->a[1];
    u16 heap = (u16)call->a[3];
    struct BagSlot *slots = NULL;
    u32 count = 0u;

    memset(&out->item_add, 0, sizeof(out->item_add));
    out->item_add.item = item;
    out->item_add.count = (u16)call->a[2];
    out->item_add.source = item_source();
    if (bag != NULL) {
        out->item_add.pocket =
            (u8)MYITEM_SAVEDATA_GetPocketOfItem(bag, item, &slots, &count, heap);
    }
}

/*
 * item.add: the count a handler changed is the count the game adds.  A
 * count of 0 leaves the item out of the bag, which the game's own text
 * does not say (docs/mod-events.md, section 1, "Items, money and healing").
 */
static void apply_item_add(const BwEventPayload *in, BwEventCall *call)
{
    call->a[2] = (uintptr_t)in->item_add.count;
}

/*
 * item.use: the party screen checks whether an item can be used on a
 * Pokemon.  The item is the argument; the target is the Pokemon the check
 * is made for.
 *
 * The target's position in the party reads 0: the call carries the party
 * record but not the party, and the party is the party screen's own work,
 * which nothing hands to the port here.  The record's address is in the
 * handle (as far as the ABI's u32 holds it; the state layer owns what the
 * handle means), so a mod that has the party can find the position itself.
 */
/*
 * A party reference to a game Pokemon record.  The slot is its position in
 * the party; a record that is not in the party (the screen's own copy, say)
 * gives the empty reference.
 */
static void party_ref_of(BwMonRef *ref, const struct PokemonParam *mon)
{
    int slot = bw_mod_state_party_slot(mon);

    memset(ref, 0, sizeof(*ref));
    if (slot < 0) {
        return;
    }
    ref->handle = (u32)(uintptr_t)mon;
    ref->place = (u8)BW_MON_PARTY;
    ref->slot = (u8)slot;
}

static void build_item_use(const BwEventCall *call, BwEventPayload *out)
{
    struct PokemonParam *mon = (struct PokemonParam *)call->a[0];

    memset(&out->item_use, 0, sizeof(out->item_use));
    out->item_use.item = (u16)call->a[1];
    party_ref_of(&out->item_use.target, mon);
}

/*
 * item.use: the refused outcome is the game's own "it won't have any
 * effect": the check answers 0, the item stays in the bag
 * (docs/mod-implementation.md, section 1, "Refusal functions").
 */
static void refuse_item_use(BwEventCall *call)
{
    call->ret = 0;
}

/*
 * shop.buy: the player picked an item and the shop checks the money and
 * the bag space.  The item and the price are the shop's own fields; the
 * price is the one the shop shows and charges
 * (docs/mod-events.md, section 1, "Items, money and healing").
 */
static void build_shop_buy(const BwEventCall *call, BwEventPayload *out)
{
    const BwPortShopWork *shop = (const BwPortShopWork *)call->a[0];

    memset(&out->shop_buy, 0, sizeof(out->shop_buy));
    if (shop != NULL) {
        out->shop_buy.item = shop->itemId;
        out->shop_buy.price = shop->price;
    }
}

/*
 * shop.buy: the price a handler changed is the price the shop shows and
 * charges.  The function reads it after the applier has run, and the
 * charge later reads it again.
 */
static void apply_shop_buy(const BwEventPayload *in, BwEventCall *call)
{
    BwPortShopWork *shop = (BwPortShopWork *)call->a[0];

    if (shop != NULL) {
        shop->price = in->shop_buy.price;
    }
}

/*
 * shop.buy: the refused outcome takes the shop back to its item list, with
 * no message of its own: no money changes hands, because the money is only
 * taken later (SCRCMD_PROC_FLD_PayPrice)
 * (docs/mod-implementation.md, section 1, "Refusal functions").
 */
static void refuse_shop_buy(BwEventCall *call)
{
    BwPortShopWork *shop = (BwPortShopWork *)call->a[0];

    if (shop != NULL) {
        shop->state = (u8)BW_PORT_SHOP_STATE_LIST_RESUME;
        shop->nextState = (u8)BW_PORT_SHOP_STATE_LIST_RESUME;
    }
}

/*
 * money.change: the money goes up (MISC_AddMoney, which
 * docs/mod-events.md calls BEACON_AddPoints).  The change is the argument;
 * the money before is read out of the misc save block.
 */
/*
 * money.change: why.  The game's two money calls take no reason, so the
 * port reads it off the function the call runs inside: every declared
 * function is on the scope stack while it runs (sdk/mod_event.c).
 */
static const struct {
    unsigned int region;
    unsigned int addr;
    u8 source;
} k_money_callers[] = {
    { BW_MOD_REGION_OVL(10), 0x0215C558u, (u8)BW_MONEY_SCRIPT },   /* SCRCMD_PROC_AddMoney */
    { BW_MOD_REGION_OVL(10), 0x0215C584u, (u8)BW_MONEY_SCRIPT },   /* SCRCMD_PROC_SubMoney */
    { BW_MOD_REGION_OVL(21), 0x021B72CCu, (u8)BW_MONEY_SCRIPT },   /* SCRCMD_POKEMON_FLD_AddTableValueToPoints */
    { BW_MOD_REGION_OVL(21), 0x021B3568u, (u8)BW_MONEY_PURCHASE }, /* SCRCMD_PROC_FLD_PayPrice */
    { BW_MOD_REGION_OVL(18), 0x0218261Cu, (u8)BW_MONEY_PURCHASE }, /* EVENT_COMM_COMMON_ItemPurchaseEventMain */
    { BW_MOD_REGION_OVL(70), 0x021B7B80u, (u8)BW_MONEY_SALE },     /* ITEMMENU_TaskSellConfirmInput */
    { BW_MOD_REGION_OVL(92), 0x021B9614u, (u8)BW_MONEY_BATTLE }    /* EVENT_BATTLE_RETURN_Main */
};

static u8 money_source(void)
{
    u32 i;

    for (i = 0; i < (u32)(sizeof(k_money_callers) / sizeof(k_money_callers[0])); i++) {
        if (bw_mod_event_scope_matches(k_money_callers[i].region,
                                       k_money_callers[i].addr, BW_SCOPE_INSIDE)) {
            return k_money_callers[i].source;
        }
    }
    return (u8)BW_MONEY_OTHER;
}

static void build_money_add(const BwEventCall *call, BwEventPayload *out)
{
    struct MiscSave *misc = (struct MiscSave *)call->a[0];

    memset(&out->money_change, 0, sizeof(out->money_change));
    out->money_change.delta = (s32)call->a[1];
    out->money_change.source = money_source();
    if (misc != NULL) {
        out->money_change.before = MISC_GetMoney(misc);
    }
}

/*
 * money.change: the money goes down (MISC_SubMoney, which
 * docs/mod-events.md calls BEACON_SubPoints).  The change is negative,
 * because the argument is the amount that is taken away.
 */
static void build_money_sub(const BwEventCall *call, BwEventPayload *out)
{
    struct MiscSave *misc = (struct MiscSave *)call->a[0];

    memset(&out->money_change, 0, sizeof(out->money_change));
    out->money_change.delta = -(s32)call->a[1];
    out->money_change.source = money_source();
    if (misc != NULL) {
        out->money_change.before = MISC_GetMoney(misc);
    }
}

/*
 * money.change: the amount a handler changed is the amount the game moves.
 * The row adds, so a change of the other sign cannot be applied here: the
 * port applies 0 and the money stays as it was.  A mod that wants the
 * money to go down waits for the row that takes it away.
 */
static void apply_money_add(const BwEventPayload *in, BwEventCall *call)
{
    if (in->money_change.delta > 0) {
        call->a[1] = (uintptr_t)in->money_change.delta;
    } else {
        call->a[1] = 0u;
    }
}

/*
 * money.change: the row takes the money away, so a change of the other
 * sign cannot be applied here.
 */
static void apply_money_sub(const BwEventPayload *in, BwEventCall *call)
{
    if (in->money_change.delta < 0) {
        call->a[1] = (uintptr_t)(-(s64)in->money_change.delta);
    } else {
        call->a[1] = 0u;
    }
}

/*
 * heal: a centre or a script heals the party (SCRCMD_PROC_HealParty).  The
 * source is a script: the Pokemon Centre's nurse runs the same script
 * command, so the two cannot be told apart at the call, and the port says
 * script.  The mask names the party positions the heal covers.
 */
static void build_heal_script(const BwEventCall *call, BwEventPayload *out)
{
    const struct Party *party = script_party(call->a[1]);

    memset(&out->heal, 0, sizeof(out->heal));
    out->heal.source = (u8)BW_HEAL_SCRIPT;
    out->heal.mask = party_mask(party);
    /* The script that runs the heal: a Pokemon Centre's nurse is one
     * script, and a mod tells it from the others by its id. */
    if (SCRCMD_WORK_GetScriptId != NULL && call->a[1] != 0u) {
        out->heal.script_id = SCRCMD_WORK_GetScriptId((void *)call->a[1]);
    }
}

/*
 * heal: the HP-restore power (GPOWER_UseHpRestore).  The source is the
 * power; the party is the argument.
 */
static void build_heal_power(const BwEventCall *call, BwEventPayload *out)
{
    const struct Party *party = (const struct Party *)call->a[0];

    memset(&out->heal, 0, sizeof(out->heal));
    out->heal.source = (u8)BW_HEAL_POWER;
    out->heal.mask = party_mask(party);
}

/*
 * heal: the positions a handler kept in the mask are healed by the port
 * itself, with the game's own healer.  The port cannot take the heal away
 * from a position the game's function heals on its own: that function
 * heals every occupied slot of the party, and there is no hook between its
 * heal and its return.  A mod that wants a narrower heal refuses the
 * event; the refusal leaves the party as it was, and the mod then heals
 * the Pokemon it wants through the state calls.
 */
static void apply_heal_mask(const BwEventPayload *in, const struct Party *party)
{
    u32 count;
    u32 i;

    if (party == NULL) {
        return;
    }
    count = POKEPARTY_GetCount(party);
    if (count > 6u) {
        count = 6u;
    }
    for (i = 0; i < count; i++) {
        if ((in->heal.mask & (1u << i)) != 0u) {
            POKE_TOOL_HealFull(POKEPARTY_GetMember((struct Party *)party, i));
        }
    }
}

static void apply_heal_script(const BwEventPayload *in, BwEventCall *call)
{
    apply_heal_mask(in, script_party(call->a[1]));
}

static void apply_heal_power(const BwEventPayload *in, BwEventCall *call)
{
    apply_heal_mask(in, (const struct Party *)call->a[0]);
}

/*
 * heal: the refused outcome is that the heal does not happen; the
 * script's own text still plays, because the script goes on
 * (docs/mod-implementation.md, section 1, "Refusal functions").  The
 * script command's own result is 0, so the refusal returns 0.
 */
static void refuse_heal_script(BwEventCall *call)
{
    call->ret = 0;
}

/*
 * heal: the refused outcome is that the power restores nothing.  The
 * function answers 0xFF when no power is active, so the refusal answers
 * 0xFF; the power itself is not consumed, the way a refused item stays in
 * the bag.
 */
static void refuse_heal_power(BwEventCall *call)
{
    call->ret = 0xFF;
}

/*
 * heal.hp on the field: PLIST_DEMO_RestoreHp(mon, hp, maxHp, amount), the
 * party screen's HP restore.  The game's amount is a number or a code:
 * 0xFF is all of max HP, 0xFE half, 0xFD a quarter, and a Pokemon whose
 * max HP is 1 always gets 1.  The payload carries the HP the code means.
 */
static u32 field_heal_amount(u32 maxHp, u32 amount)
{
    if (maxHp == 1u) {
        return 1u;
    }
    if (amount == 0xFFu) {
        return maxHp;
    }
    if (amount == 0xFEu) {
        return maxHp >> 1;
    }
    if (amount == 0xFDu) {
        return maxHp >> 2;
    }
    return amount;
}

static void build_heal_hp_field(const BwEventCall *call, BwEventPayload *out)
{
    u32 maxHp = (u32)call->a[2];

    memset(&out->heal_hp, 0, sizeof(out->heal_hp));
    party_ref_of(&out->heal_hp.mon, (const struct PokemonParam *)call->a[0]);
    out->heal_hp.hp = (u16)call->a[1];
    out->heal_hp.max_hp = (u16)maxHp;
    out->heal_hp.amount = (u16)field_heal_amount(maxHp, (u32)call->a[3]);
    out->heal_hp.source = (u8)BW_HEAL_HP_FIELD;
}

/*
 * The new amount goes back as the game's own argument.  A number the game
 * reads as a code (0xFD to 0xFF) cannot be passed as it is: the call then
 * gets the code for "all of max HP" and a max HP of hp + amount, so the
 * game adds max HP and caps the result at hp + amount, which is the amount
 * asked for.  The cap is never above the real max HP.
 */
static void apply_heal_hp_field(const BwEventPayload *in, BwEventCall *call)
{
    u32 hp = (u32)call->a[1];
    u32 maxHp = (u32)call->a[2];
    u32 amount = in->heal_hp.amount;

    if (maxHp == 1u) {
        if (amount == 0u) {
            call->a[2] = (uintptr_t)hp;     /* the cap keeps the HP it has */
            call->a[3] = 0u;
        }
        return;
    }
    if (amount < 0xFDu) {
        call->a[3] = (uintptr_t)amount;
        return;
    }
    call->a[2] = (uintptr_t)(hp + amount < maxHp ? hp + amount : maxHp);
    call->a[3] = 0xFFu;
}

/* heal.hp: the refused outcome is that the HP stays as it is.  The
 * function answers nothing. */
static void refuse_heal_hp_field(BwEventCall *call)
{
    call->ret = 0;
}

/*
 * mon.revive on the field.  The party screen asks PLIST_DEMO_CheckItemUsable
 * (mon, item, ...) before it uses an item and takes the item from the bag
 * only when the check says yes, so the check is where a refusal keeps the
 * item.  The HP the handlers chose is written where the game adds it,
 * PLIST_DEMO_RestoreHp, by the port's own row below.  Sacred Ash revives
 * the party without the check: the event fires at the HP write then, and a
 * refusal there keeps the Pokemon fainted (the ash is used).
 */
static struct {
    const struct PokemonParam *mon;
    u16 hp;
} g_field_revive;

static u32 revive_hp_of_item(u32 item, u32 maxHp)
{
    u32 amount;

    if (FILE_UTIL_GetItemParam == NULL) {
        return maxHp;
    }
    amount = FILE_UTIL_GetItemParam(item, BW_ITEM_PARAM_HP_AMOUNT, BW_ITEM_HEAP);
    return field_heal_amount(maxHp, amount);
}

static int item_revives(u32 item)
{
    if (FILE_UTIL_GetItemParam == NULL) {
        return 0;
    }
    return (FILE_UTIL_GetItemParam(item, BW_ITEM_PARAM_REVIVES, BW_ITEM_HEAP) != 0u ||
            FILE_UTIL_GetItemParam(item, BW_ITEM_PARAM_REVIVES_FULL, BW_ITEM_HEAP) != 0u) &&
           FILE_UTIL_GetItemParam(item, BW_ITEM_PARAM_RAISES_LEVEL, BW_ITEM_HEAP) == 0u;
}

static int when_field_revive_check(const BwEventCall *call)
{
    struct PokemonParam *mon = (struct PokemonParam *)call->a[0];

    return mon != NULL && POKE_TOOL_GetPartyValue != NULL &&
           POKE_TOOL_GetPartyValue(mon, BW_FIELD_HP, 0) == 0u &&
           item_revives((u32)call->a[1]);
}

static void build_mon_revive_field(const BwEventCall *call, BwEventPayload *out)
{
    struct PokemonParam *mon = (struct PokemonParam *)call->a[0];
    u32 maxHp = POKE_TOOL_GetPartyValue(mon, BW_FIELD_MAX_HP, 0);

    memset(&out->mon_revive, 0, sizeof(out->mon_revive));
    party_ref_of(&out->mon_revive.mon, mon);
    out->mon_revive.max_hp = (u16)maxHp;
    out->mon_revive.hp = (u16)revive_hp_of_item((u32)call->a[1], maxHp);
    out->mon_revive.item = (u16)call->a[1];
    out->mon_revive.source = (u8)BW_REVIVE_FIELD_ITEM;
}

static void apply_mon_revive_field(const BwEventPayload *in, BwEventCall *call)
{
    g_field_revive.mon = (const struct PokemonParam *)call->a[0];
    g_field_revive.hp = in->mon_revive.hp;
}

/* mon.revive at the check: refused, the item has no effect and stays. */
static void refuse_mon_revive_field(BwEventCall *call)
{
    call->ret = 0;
}

/* The HP write: (mon, hp, maxHp, amount).  heal.hp sees the Pokemon that
 * have HP; the port's own row finishes a revive. */
static int when_restore_alive(const BwEventCall *call)
{
    return (u32)call->a[1] != 0u;
}

static int when_restore_fainted(const BwEventCall *call)
{
    return (u32)call->a[1] == 0u;
}

/* Write `hp` as the HP a fainted Pokemon comes back with, through the
 * game's own arguments (see apply_heal_hp_field for the code values). */
static void restore_set_revive_hp(BwEventCall *call, u32 hp)
{
    u32 maxHp = (u32)call->a[2];

    if (hp > maxHp) {
        hp = maxHp;
    }
    if (hp < 0xFDu) {
        call->a[3] = (uintptr_t)hp;
        return;
    }
    call->a[2] = (uintptr_t)hp;     /* the cap is the HP asked for */
    call->a[3] = 0xFFu;
}

static void apply_restore_revive(const BwEventPayload *unused, BwEventCall *call)
{
    struct PokemonParam *mon = (struct PokemonParam *)call->a[0];
    BwEventPayload p;
    u32 maxHp = (u32)call->a[2];
    u32 hp;

    (void)unused;
    if (mon != NULL && g_field_revive.mon == mon) {
        hp = g_field_revive.hp;
        g_field_revive.mon = NULL;
    } else {
        /* No check came first (Sacred Ash): the event fires here. */
        memset(&p, 0, sizeof p);
        party_ref_of(&p.mon_revive.mon, mon);
        p.mon_revive.max_hp = (u16)maxHp;
        p.mon_revive.hp = (u16)field_heal_amount(maxHp, (u32)call->a[3]);
        p.mon_revive.source = (u8)BW_REVIVE_FIELD_ITEM;
        if (bw_mod_event_fire(BW_EVENT_MON_REVIVE, &p)) {
            call->a[3] = 0u;    /* it stays fainted */
            if (maxHp == 1u) {
                call->a[2] = 0u;
            }
            return;
        }
        hp = p.mon_revive.hp;
    }
    restore_set_revive_hp(call, hp < 1u ? 1u : hp);
}

/*
 * A party heal (a Centre or another script, the HP-restore power) as a
 * whole is the `heal` event.  Each member gets its own event too, for
 * what the heal did to it: mon.revive when it had fainted, heal.hp when it
 * had lost HP, mon.cure when it had a status.  The game's function heals
 * the members it chooses in one go (the power may heal only the first
 * one), so the port notes each member before the call and, after it, asks
 * about each change the call made and puts back what a handler refused:
 * the 0 HP of a fainted member, the HP a member had, its status.  A refusal
 * of `heal` itself stops the whole heal before any member event.
 */
static struct {
    struct Party *party;
    u8  count;
    u8  revive_source;
    u16 hp[6];          /* member i's HP before the heal */
    u32 status[6];      /* member i's status before the heal */
} g_party_heal;

static void party_heal_before(struct Party *party, u8 revive_source)
{
    u32 count, i;

    memset(&g_party_heal, 0, sizeof g_party_heal);
    if (party == NULL || POKEPARTY_GetCount == NULL || POKEPARTY_GetMember == NULL ||
        POKE_TOOL_GetPartyValue == NULL) {
        return;
    }
    count = POKEPARTY_GetCount(party);
    if (count > 6u) {
        count = 6u;
    }
    g_party_heal.party = party;
    g_party_heal.count = (u8)count;
    g_party_heal.revive_source = revive_source;
    for (i = 0; i < count; i++) {
        struct PokemonParam *mon = POKEPARTY_GetMember(party, i);

        if (mon != NULL) {
            g_party_heal.hp[i] = (u16)POKE_TOOL_GetPartyValue(mon, BW_FIELD_HP, 0);
            g_party_heal.status[i] = POKE_TOOL_GetPartyValue(mon, BW_FIELD_STATUS, 0);
        }
    }
}

static void party_heal_after(void)
{
    u32 i;

    if (g_party_heal.party == NULL || POKE_TOOL_SetPartyValue == NULL) {
        return;
    }
    for (i = 0; i < g_party_heal.count; i++) {
        struct PokemonParam *mon = POKEPARTY_GetMember(g_party_heal.party, i);
        BwEventPayload p;
        u32 before = g_party_heal.hp[i];
        u32 hp, maxHp, status;

        if (mon == NULL || POKE_TOOL_GetPartyValue(mon, 0x4Cu, 0) != 0u) {
            continue;   /* gone, or an egg */
        }
        hp = POKE_TOOL_GetPartyValue(mon, BW_FIELD_HP, 0);
        maxHp = POKE_TOOL_GetPartyValue(mon, BW_FIELD_MAX_HP, 0);
        status = POKE_TOOL_GetPartyValue(mon, BW_FIELD_STATUS, 0);

        if (before == 0u && hp != 0u) {
            memset(&p, 0, sizeof p);
            party_ref_of(&p.mon_revive.mon, mon);
            p.mon_revive.max_hp = (u16)maxHp;
            p.mon_revive.hp = (u16)hp;
            p.mon_revive.source = g_party_heal.revive_source;
            if (bw_mod_event_fire(BW_EVENT_MON_REVIVE, &p)) {
                /* It stays fainted, with the status it had. */
                POKE_TOOL_SetPartyValue(mon, BW_FIELD_HP, 0u);
                POKE_TOOL_SetPartyValue(mon, BW_FIELD_STATUS, g_party_heal.status[i]);
                continue;
            }
            if (p.mon_revive.hp != hp) {
                u32 v = p.mon_revive.hp == 0u ? 1u : p.mon_revive.hp;

                POKE_TOOL_SetPartyValue(mon, BW_FIELD_HP, v > maxHp ? maxHp : v);
            }
        } else if (before != 0u && hp > before) {
            memset(&p, 0, sizeof p);
            party_ref_of(&p.heal_hp.mon, mon);
            p.heal_hp.hp = (u16)before;
            p.heal_hp.max_hp = (u16)maxHp;
            p.heal_hp.amount = (u16)(hp - before);
            p.heal_hp.source = (u8)BW_HEAL_HP_PARTY;
            if (bw_mod_event_fire(BW_EVENT_HEAL_HP, &p)) {
                POKE_TOOL_SetPartyValue(mon, BW_FIELD_HP, before);
            } else if (before + p.heal_hp.amount != hp) {
                u32 v = before + p.heal_hp.amount;

                POKE_TOOL_SetPartyValue(mon, BW_FIELD_HP, v > maxHp ? maxHp : v);
            }
        }
        if (before != 0u && g_party_heal.status[i] != 0u && status == 0u) {
            memset(&p, 0, sizeof p);
            party_ref_of(&p.mon_cure.mon, mon);
            p.mon_cure.status = (u8)g_party_heal.status[i];
            p.mon_cure.source = (u8)BW_CURE_PARTY;
            if (bw_mod_event_fire(BW_EVENT_MON_CURE, &p)) {
                POKE_TOOL_SetPartyValue(mon, BW_FIELD_STATUS, g_party_heal.status[i]);
            }
        }
    }
    g_party_heal.party = NULL;
}

static void apply_revive_script_before(const BwEventPayload *unused, BwEventCall *call)
{
    (void)unused;
    party_heal_before((struct Party *)script_party(call->a[1]),
                        (u8)BW_REVIVE_SCRIPT);
}

static void apply_revive_power_before(const BwEventPayload *unused, BwEventCall *call)
{
    (void)unused;
    party_heal_before((struct Party *)call->a[0], (u8)BW_REVIVE_POWER);
}

static void apply_revive_after(const BwEventPayload *unused, BwEventCall *call)
{
    (void)unused;
    (void)call;
    party_heal_after();
}

/*
 * mon.levelup from an item: the party screen's PLIST_DEMO_UseItemOnMon
 * (mon, item, ...) sets a Rare Candy's experience and works the level out
 * again in place.  The port reads the level before the call and after it,
 * and fires the event when it went up.  The event cannot be refused: a mod
 * that wants no Rare Candy refuses item.use, which keeps the candy.
 */
static struct {
    const struct PokemonParam *mon;
    u8 level;
} g_item_level;

#define BW_FIELD_LEVEL 0x9Eu

static void apply_item_level_before(const BwEventPayload *unused, BwEventCall *call)
{
    struct PokemonParam *mon = (struct PokemonParam *)call->a[0];

    (void)unused;
    g_item_level.mon = mon;
    g_item_level.level = (mon != NULL && POKE_TOOL_GetPartyValue != NULL)
                             ? (u8)POKE_TOOL_GetPartyValue(mon, BW_FIELD_LEVEL, 0) : 0u;
}

static void apply_item_level_after(const BwEventPayload *unused, BwEventCall *call)
{
    struct PokemonParam *mon = (struct PokemonParam *)call->a[0];
    BwEventPayload p;
    u8 level;

    (void)unused;
    if (mon == NULL || g_item_level.mon != mon || POKE_TOOL_GetPartyValue == NULL) {
        return;
    }
    g_item_level.mon = NULL;
    level = (u8)POKE_TOOL_GetPartyValue(mon, BW_FIELD_LEVEL, 0);
    if (level <= g_item_level.level) {
        return;
    }
    memset(&p, 0, sizeof p);
    party_ref_of(&p.mon_levelup.mon, mon);
    p.mon_levelup.from = g_item_level.level;
    p.mon_levelup.to = level;
    p.mon_levelup.source = (u8)BW_LEVELUP_ITEM;
    p.mon_levelup.item = (u16)call->a[1];
    (void)bw_mod_event_fire(BW_EVENT_MON_LEVELUP, &p);
}

/*
 * mon.cure on the party screen: at the item check, when the item cures the
 * status the Pokemon has.  Refused, the check answers no and the item stays
 * (a Full Restore then heals nothing either: it is one item).
 */
static int item_cures_status(u32 item, u32 status)
{
    u32 sel;

    switch (status) {
    case 1u: sel = BW_ITEM_PARAM_CURES_PARALYSIS; break;
    case 2u: sel = BW_ITEM_PARAM_CURES_SLEEP; break;
    case 3u: sel = BW_ITEM_PARAM_CURES_FREEZE; break;
    case 4u: sel = BW_ITEM_PARAM_CURES_BURN; break;
    case 5u: sel = BW_ITEM_PARAM_CURES_POISON; break;
    default: return 0;
    }
    return FILE_UTIL_GetItemParam != NULL &&
           FILE_UTIL_GetItemParam(item, sel, BW_ITEM_HEAP) != 0u;
}

static int when_field_cure_check(const BwEventCall *call)
{
    struct PokemonParam *mon = (struct PokemonParam *)call->a[0];

    return mon != NULL && POKE_TOOL_GetPartyValue != NULL &&
           POKE_TOOL_GetPartyValue(mon, BW_FIELD_HP, 0) != 0u &&
           item_cures_status((u32)call->a[1],
                             POKE_TOOL_GetPartyValue(mon, BW_FIELD_STATUS, 0));
}

static void build_mon_cure_field(const BwEventCall *call, BwEventPayload *out)
{
    struct PokemonParam *mon = (struct PokemonParam *)call->a[0];

    memset(&out->mon_cure, 0, sizeof(out->mon_cure));
    party_ref_of(&out->mon_cure.mon, mon);
    out->mon_cure.status = (u8)POKE_TOOL_GetPartyValue(mon, BW_FIELD_STATUS, 0);
    out->mon_cure.source = (u8)BW_CURE_FIELD_ITEM;
    out->mon_cure.item = (u16)call->a[1];
}

/* mon.cure at the check, refused: the item has no effect and stays. */
static void refuse_mon_cure_field(BwEventCall *call)
{
    call->ret = 0;
}

/*
 * mon.pp on the party screen: POKE_TOOL_RestorePP (mon, slot, amount), the
 * PP restore an Ether or an Elixir runs per move.  The game's amount 0x7F
 * is "to the max"; the payload carries the PP that means.
 */
#define BW_FIELD_MOVE1_ID  0x36u
#define BW_FIELD_PP1_ID    0x3Au
#define BW_FIELD_MAXPP1_ID 0x42u

static void build_mon_pp_field(const BwEventCall *call, BwEventPayload *out)
{
    struct PokemonParam *mon = (struct PokemonParam *)call->a[0];
    u32 slot = (u32)call->a[1];
    u32 amount = (u32)call->a[2];

    memset(&out->mon_pp, 0, sizeof(out->mon_pp));
    party_ref_of(&out->mon_pp.mon, mon);
    out->mon_pp.slot = (u8)slot;
    out->mon_pp.source = (u8)BW_PP_FIELD_ITEM;
    if (mon != NULL && POKE_TOOL_GetPartyValue != NULL && slot < 4u) {
        out->mon_pp.move = (u16)POKE_TOOL_GetPartyValue(mon, BW_FIELD_MOVE1_ID + slot, 0);
        if (amount == 0x7Fu) {
            u32 cur = POKE_TOOL_GetPartyValue(mon, BW_FIELD_PP1_ID + slot, 0);
            u32 max = POKE_TOOL_GetPartyValue(mon, BW_FIELD_MAXPP1_ID + slot, 0);

            amount = max > cur ? max - cur : 0u;
        }
    }
    out->mon_pp.amount = (u16)amount;
}

static void apply_mon_pp_field(const BwEventPayload *in, BwEventCall *call)
{
    /* 0x7F is the game's "to the max": a real amount stays below it. */
    call->a[2] = (uintptr_t)(in->mon_pp.amount >= 0x7Fu ? 0x7Eu : in->mon_pp.amount);
}

/* mon.pp refused: no PP; the function answers 0, "no change". */
static void refuse_mon_pp_field(BwEventCall *call)
{
    call->ret = 0;
}

/*
 * mon.form outside a battle: POKE_TOOL_ChangeForm (mon, form), which the
 * form items, the seasons and the scripts all run.  It answers 1 when the
 * form changed; refused, it answers 0 and the form stays.
 */
static int when_form_changes(const BwEventCall *call)
{
    struct PokemonParam *mon = (struct PokemonParam *)call->a[0];

    return mon != NULL && POKE_TOOL_GetPartyValue != NULL &&
           POKE_TOOL_GetPartyValue(mon, BW_FIELD_FORM_ID, 0) != (u32)call->a[1];
}

static void build_mon_form_field(const BwEventCall *call, BwEventPayload *out)
{
    struct PokemonParam *mon = (struct PokemonParam *)call->a[0];

    memset(&out->mon_form, 0, sizeof(out->mon_form));
    party_ref_of(&out->mon_form.mon, mon);
    out->mon_form.species = (u16)POKE_TOOL_GetPartyValue(mon, BW_FIELD_SPECIES_ID, 0);
    out->mon_form.from = (u8)POKE_TOOL_GetPartyValue(mon, BW_FIELD_FORM_ID, 0);
    out->mon_form.to = (u8)call->a[1];
    out->mon_form.source = (u8)BW_FORM_FIELD;
}

static void apply_mon_form_field(const BwEventPayload *in, BwEventCall *call)
{
    call->a[1] = (uintptr_t)in->mon_form.to;
}

static void refuse_mon_form_field(BwEventCall *call)
{
    call->ret = 0;
}

/* ------------------------------------------------------------------ */
/* The rows                                                            */
/* ------------------------------------------------------------------ */

/*
 * The rows of this area.  The scope of every row is BW_SCOPE_ANY: none of
 * these events only means something while a particular caller runs
 * (docs/mod-events.md, section 1).
 */
static const BwEventBinding g_items_rows[] = {
    { BW_MOD_REGION_ARM9, 0x02007E50u, BW_EVENT_ITEM_ADD, BW_EVENT_BEFORE,
      BW_SCOPE_ANY, 0u, 0u, build_item_add, apply_item_add, NULL, NULL },

    { BW_MOD_REGION_OVL(91), 0x021BDD78u, BW_EVENT_ITEM_USE, BW_EVENT_BEFORE,
      BW_SCOPE_ANY, 0u, 0u, build_item_use, NULL, refuse_item_use, NULL },

    { BW_MOD_REGION_OVL(91), 0x021BDD78u, BW_EVENT_MON_REVIVE, BW_EVENT_BEFORE,
      BW_SCOPE_ANY, 0u, 0u, build_mon_revive_field, apply_mon_revive_field,
      refuse_mon_revive_field, when_field_revive_check },

    { BW_MOD_REGION_OVL(91), 0x021BDD78u, BW_EVENT_MON_CURE, BW_EVENT_BEFORE,
      BW_SCOPE_ANY, 0u, 0u, build_mon_cure_field, NULL, refuse_mon_cure_field,
      when_field_cure_check },

    { BW_MOD_REGION_ARM9, 0x0201B970u, BW_EVENT_MON_PP, BW_EVENT_BEFORE,
      BW_SCOPE_ANY, 0u, 0u, build_mon_pp_field, apply_mon_pp_field,
      refuse_mon_pp_field, NULL },

    { BW_MOD_REGION_ARM9, 0x02017988u, BW_EVENT_MON_FORM, BW_EVENT_BEFORE,
      BW_SCOPE_ANY, 0u, 0u, build_mon_form_field, apply_mon_form_field,
      refuse_mon_form_field, when_form_changes },

    { BW_MOD_REGION_OVL(91), 0x021BE2B4u, BW_EVENT_PORT, BW_EVENT_BEFORE,
      BW_SCOPE_ANY, 0u, 0u, NULL, apply_item_level_before, NULL, NULL },

    { BW_MOD_REGION_OVL(91), 0x021BE2B4u, BW_EVENT_PORT, BW_EVENT_AFTER,
      BW_SCOPE_ANY, 0u, 0u, NULL, apply_item_level_after, NULL, NULL },

    { BW_MOD_REGION_OVL(91), 0x021BE914u, BW_EVENT_HEAL_HP, BW_EVENT_BEFORE,
      BW_SCOPE_ANY, 0u, 0u, build_heal_hp_field, apply_heal_hp_field,
      refuse_heal_hp_field, when_restore_alive },

    { BW_MOD_REGION_OVL(91), 0x021BE914u, BW_EVENT_PORT, BW_EVENT_BEFORE,
      BW_SCOPE_ANY, 0u, 0u, NULL, apply_restore_revive, NULL,
      when_restore_fainted },

    { BW_MOD_REGION_OVL(21), 0x021B3370u, BW_EVENT_SHOP_BUY, BW_EVENT_BEFORE,
      BW_SCOPE_ANY, 0u, 0u, build_shop_buy, apply_shop_buy, refuse_shop_buy, NULL },

    { BW_MOD_REGION_ARM9, 0x0200C278u, BW_EVENT_MONEY_CHANGE, BW_EVENT_BEFORE,
      BW_SCOPE_ANY, 0u, 0u, build_money_add, apply_money_add, NULL, NULL },

    { BW_MOD_REGION_ARM9, 0x0200C29Cu, BW_EVENT_MONEY_CHANGE, BW_EVENT_BEFORE,
      BW_SCOPE_ANY, 0u, 0u, build_money_sub, apply_money_sub, NULL, NULL },

    { BW_MOD_REGION_OVL(10), 0x0215B5A8u, BW_EVENT_HEAL, BW_EVENT_BEFORE,
      BW_SCOPE_ANY, 0u, 0u, build_heal_script, apply_heal_script,
      refuse_heal_script, NULL },

    { BW_MOD_REGION_OVL(10), 0x0215B5A8u, BW_EVENT_PORT, BW_EVENT_BEFORE,
      BW_SCOPE_ANY, 0u, 0u, NULL, apply_revive_script_before, NULL, NULL },

    { BW_MOD_REGION_OVL(10), 0x0215B5A8u, BW_EVENT_PORT, BW_EVENT_AFTER,
      BW_SCOPE_ANY, 0u, 0u, NULL, apply_revive_after, NULL, NULL },

    { BW_MOD_REGION_ARM9, 0x0201104Cu, BW_EVENT_HEAL, BW_EVENT_BEFORE,
      BW_SCOPE_ANY, 0u, 0u, build_heal_power, apply_heal_power,
      refuse_heal_power, NULL },

    { BW_MOD_REGION_ARM9, 0x0201104Cu, BW_EVENT_PORT, BW_EVENT_BEFORE,
      BW_SCOPE_ANY, 0u, 0u, NULL, apply_revive_power_before, NULL, NULL },

    { BW_MOD_REGION_ARM9, 0x0201104Cu, BW_EVENT_PORT, BW_EVENT_AFTER,
      BW_SCOPE_ANY, 0u, 0u, NULL, apply_revive_after, NULL, NULL }
};

/* The rows of this area, for the dispatch core. */
const BwEventBinding *bw_event_bindings_items(u32 *count)
{
    if (count != NULL) {
        *count = (u32)(sizeof(g_items_rows) / sizeof(g_items_rows[0]));
    }
    return g_items_rows;
}
