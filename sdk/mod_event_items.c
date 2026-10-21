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

/*
 * The script command's own work, copied member for member from the game's
 * ov010 header (aletheia src/game/ov010/scrcmd_work.c).  The heal script
 * command takes it and reads the game data out of it (SCRCMD_WORK_GetGameData
 * is `work->common->gameData`); the game publishes the work only to the
 * script VM, so the port reads it through this view.  The common work's
 * first three members are the script work, the game system and the game
 * data, and the port needs the third.
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
BW_PORT_STATIC_ASSERT(scrcmd_work_common, sizeof(void *) != 4 ||
                      offsetof(BwPortScrcmdWork, common) == 0x020u);

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
static void build_money_add(const BwEventCall *call, BwEventPayload *out)
{
    struct MiscSave *misc = (struct MiscSave *)call->a[0];

    memset(&out->money_change, 0, sizeof(out->money_change));
    out->money_change.delta = (s32)call->a[1];
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
      BW_SCOPE_ANY, 0u, 0u, build_item_add, apply_item_add, NULL },

    { BW_MOD_REGION_OVL(91), 0x021BDD78u, BW_EVENT_ITEM_USE, BW_EVENT_BEFORE,
      BW_SCOPE_ANY, 0u, 0u, build_item_use, NULL, refuse_item_use },

    { BW_MOD_REGION_OVL(91), 0x021BE914u, BW_EVENT_HEAL_HP, BW_EVENT_BEFORE,
      BW_SCOPE_ANY, 0u, 0u, build_heal_hp_field, apply_heal_hp_field,
      refuse_heal_hp_field },

    { BW_MOD_REGION_OVL(21), 0x021B3370u, BW_EVENT_SHOP_BUY, BW_EVENT_BEFORE,
      BW_SCOPE_ANY, 0u, 0u, build_shop_buy, apply_shop_buy, refuse_shop_buy },

    { BW_MOD_REGION_ARM9, 0x0200C278u, BW_EVENT_MONEY_CHANGE, BW_EVENT_BEFORE,
      BW_SCOPE_ANY, 0u, 0u, build_money_add, apply_money_add, NULL },

    { BW_MOD_REGION_ARM9, 0x0200C29Cu, BW_EVENT_MONEY_CHANGE, BW_EVENT_BEFORE,
      BW_SCOPE_ANY, 0u, 0u, build_money_sub, apply_money_sub, NULL },

    { BW_MOD_REGION_OVL(10), 0x0215B5A8u, BW_EVENT_HEAL, BW_EVENT_BEFORE,
      BW_SCOPE_ANY, 0u, 0u, build_heal_script, apply_heal_script,
      refuse_heal_script },

    { BW_MOD_REGION_ARM9, 0x0201104Cu, BW_EVENT_HEAL, BW_EVENT_BEFORE,
      BW_SCOPE_ANY, 0u, 0u, build_heal_power, apply_heal_power,
      refuse_heal_power }
};

/* The rows of this area, for the dispatch core. */
const BwEventBinding *bw_event_bindings_items(u32 *count)
{
    if (count != NULL) {
        *count = (u32)(sizeof(g_items_rows) / sizeof(g_items_rows[0]));
    }
    return g_items_rows;
}
