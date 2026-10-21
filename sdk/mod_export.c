/*
 * sdk/mod_export.c -- the Pokemon export and import service
 * (docs/mod-services.md, "Pokemon export and import"; docs/mod-implementation.md,
 * section 4, step 8).
 *
 * What this file is
 * -----------------
 *
 * The service that moves a Pokemon between games: out of one save into a
 * file, and out of a file into another save.
 *
 * The exported form
 * -----------------
 *
 * The game's own Pokemon record, 0xDC bytes, with the port's header in
 * front of it:
 *
 *     offset  size  what
 *     0       4     the magic, "BWPM"
 *     4       2     the format version, 1
 *     6       2     the flags, zero
 *     8       4     the record's size, 0xDC
 *     12      4     the checksum of the record's bytes (a CRC-32)
 *     16      0xDC  the game's own Pokemon record
 *
 * The header is written one field at a time and little-endian, so the file
 * is the same on every host whatever the compiler does with the struct.
 * The checksum is the CRC-32 every host library computes (the reflected
 * IEEE polynomial 0xEDB88320, the one zlib and PNG use), the same
 * checksum the saved-data layer uses (sdk/mod_savedata.c).
 *
 * What an import checks
 * ---------------------
 *
 *   - the port's own header: the magic, the version and the checksum;
 *   - the game's own record checksum: the sum of the record's block area
 *     has to be the checksum word the record carries (the game's own rule,
 *     POKE_CRYPTO_Checksum, src/game/arm9/poke_tool.c);
 *   - the species, the moves and the item: a record the game does not know
 *     is refused, because the game would not be able to read it.
 *
 * The game's own record
 * ---------------------
 *
 * The record is the game's own (src/game/arm9/poke_tool_types.h): a box
 * part of 0x88 bytes and a party part of 0x54 bytes.
 *
 *     offset  size  what
 *     0x00    4     the personality value, the seed of both masks
 *     0x04    2     the flags: partyDecoded, boxDecoded, checksumFailed
 *     0x06    2     the checksum of the block area
 *     0x08    0x80  the block area, masked
 *     0x88    0x54  the party area, masked
 *
 * The two areas are masked with a stream of the game's own: the linear
 * congruential generator `s = s * 0x41C64E6D + 0x6073`, the middle
 * halfword of each new state, XORed into the area halfword by halfword
 * (POKE_CRYPTO_XorBlock).  The party area is masked with the personality
 * and the block area with the checksum word.  The mask is its own
 * inverse, so masking and unmasking are the same operation, which is how
 * the game reads a record: it unmasks the record, reads it and masks it
 * again (POKE_TOOL_GetPartyValue brackets its read that way).
 *
 * The flags say which state the record is in: the areas are masked when
 * the two decoded bits are clear, and in the clear when they are set.  The
 * record carries its own state, so a record exported from one game and
 * imported into another is read the same way in both.
 *
 * The port implements the mask and the checksum here rather than calling
 * the game's own functions, so that a file can be checked with no game
 * running at all, and so that the two agree by construction: the algorithm
 * is the game's own, and it is written out here.
 *
 * Where the record goes
 * ---------------------
 *
 * An import puts the Pokemon into the party, and into a box when the
 * party is full (docs/mod-services.md, "Pokemon export and import").  The
 * game reads the party and the boxes through its own accessors, and the
 * port's own seam (bw_mod_export_set_game_data) is where the game data
 * comes from: the state layer (sdk/mod_state.c) binds the same object for
 * the state calls, and the port binds it here as well, wherever it binds
 * it there.
 *
 * A call that reaches the game without the game there reports a failure
 * and never calls into nothing: the game's accessors are declared weak, the
 * way sdk/mod_state.c declares them, and a build without the game leaves
 * them absent.
 *
 * What is not here
 * -----------------
 *
 * The peer sessions are built (sdk/mod_net.c), so the record can be sent to
 * another player: the save handoff carries a Pokemon's export over a
 * session (sdk/mod_handoff.c), and the bytes the service produces are the
 * bytes that handoff carries.
 */

#include <stddef.h>
#include <string.h>

#include "types.h"
#include "mod_state.h"
#include "save.h"

/* ------------------------------------------------------------------ */
/* The port's own format                                               */
/* ------------------------------------------------------------------ */

/* The magic: "BWPM", little-endian. */
#define BW_MOD_MON_MAGIC 0x4D505742u

/* The format version.  A file with another version is refused. */
#define BW_MOD_MON_VERSION 1u

/* The header's size on the disk, and the size of the whole file. */
#define BW_MOD_MON_HEADER_SIZE 16u
#define BW_MOD_MON_RECORD_SIZE 0xDCu
#define BW_MOD_MON_FILE_SIZE   (BW_MOD_MON_HEADER_SIZE + BW_MOD_MON_RECORD_SIZE)

/* The header's fields, one at a time and little-endian. */
#define BW_MOD_MON_OFF_MAGIC   0u
#define BW_MOD_MON_OFF_VERSION 4u
#define BW_MOD_MON_OFF_FLAGS   6u
#define BW_MOD_MON_OFF_SIZE    8u
#define BW_MOD_MON_OFF_CRC    12u

/* The port's own check on the header's size. */
typedef char bw_mod_mon_header_is_16[
    (BW_MOD_MON_HEADER_SIZE == 16u) ? 1 : -1];

/* ------------------------------------------------------------------ */
/* The game's own record                                               */
/* ------------------------------------------------------------------ */

/*
 * The record's flags (the halfword at +0x04 of the game's own record).
 * The two decoded bits say which state the record's two areas are in, and
 * the third is the game's own "the checksum did not match" flag.
 */
#define BW_MOD_MON_PARTY_DECODED   0x0001u
#define BW_MOD_MON_BOX_DECODED     0x0002u
#define BW_MOD_MON_CHECKSUM_FAILED 0x0004u

/*
 * The game's own Pokemon record, in the port's own spelling: the box part
 * and the party part side by side, which is the same 0xDC bytes the game
 * keeps (src/game/arm9/poke_tool_types.h).
 */
typedef struct BwModMonRecord {
    u32 personality;
    u16 flags;
    u16 checksum;
    u8  blocks[0x80];
    u8  party[0x54];
} BwModMonRecord;

/* The record is 0xDC bytes, the size the export carries. */
typedef char bw_mod_mon_record_is_dc[
    (sizeof(BwModMonRecord) == BW_MOD_MON_RECORD_SIZE) ? 1 : -1];

/* The box part is 0x88 bytes: the part a box holds. */
#define BW_MOD_MON_BOX_SIZE 0x88u

/* The field ids the game's own accessors take (src/game/arm9/
 * poke_tool_param.h).  The species, the item and the experience are the
 * ones the state table names as well (sdk/mod_state.h); the moves and the
 * original trainer's id are ids of the game's own. */
#define BW_MOD_MON_FIELD_MOVE  0x36u
#define BW_MOD_MON_FIELD_OT_ID 0x07u

/*
 * The record's block area holds four blocks of 0x20 bytes, and the game
 * shuffles them by the personality value (POKE_TOOL_GetBoxSubstruct).  The
 * species, the item and the experience are in the first block of the four.
 */
#define BW_MOD_MON_BLOCK_COUNT 4
#define BW_MOD_MON_BLOCK_SIZE  0x20u

/* The base PP is the fifth field of a move record (WAZA_TOOL_GetDataParam,
 * src/game/arm9/waza_tool.c): a move the game does not know has no record
 * and answers zero. */
#define BW_MOD_MON_WAZA_BASE_PP 5u

/* The base HP is the first field of a personal record
 * (POKE_PERSONAL_GetParam, src/game/arm9/poke_personal.c): a species the
 * game does not know has no record and answers zero. */
#define BW_MOD_MON_PERSONAL_BASE_HP 0u

/*
 * The last species of the game's own table: Arceus, 493.  A species above
 * it is not one this game knows, whatever the personal record the cache
 * holds (the cache keeps the last species it loaded, so an unknown id
 * would otherwise answer with the previous one's numbers).
 */
#define BW_MOD_MON_MAX_SPECIES 493u

/*
 * The last item of the game's own table: a/0/2/4 has 627 members, 0..626,
 * and the game's own accessor refuses any item above 626 (src/game/arm9/
 * file_util.c and poke_tool.c).
 */
#define BW_MOD_MON_MAX_ITEM 626u

/* ------------------------------------------------------------------ */
/* The checksum                                                        */
/* ------------------------------------------------------------------ */

/*
 * The record's checksum: the CRC-32 every host library computes (the
 * reflected IEEE polynomial 0xEDB88320).  The port's own choice, and the
 * same checksum the saved-data layer uses (sdk/mod_savedata.c).
 */
static u32 g_crc_table[256];
static int g_crc_table_ready;

static void crc_table_build(void)
{
    u32 i;

    for (i = 0; i < 256u; i++) {
        u32 c = i;
        int b;

        for (b = 0; b < 8; b++) {
            c = (c & 1u) != 0u ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
        }
        g_crc_table[i] = c;
    }
    g_crc_table_ready = 1;
}

u32 bw_mod_mon_crc(const void *data, u32 size)
{
    const u8 *p = (const u8 *)data;
    u32 crc = 0xFFFFFFFFu;
    u32 i;

    if (p == NULL) {
        return 0;
    }
    if (!g_crc_table_ready) {
        crc_table_build();
    }
    for (i = 0; i < size; i++) {
        crc = g_crc_table[(crc ^ p[i]) & 0xFFu] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

/* ------------------------------------------------------------------ */
/* The game's own mask                                                 */
/* ------------------------------------------------------------------ */

/*
 * The mask: the game's own linear congruential generator
 * `s = s * 0x41C64E6D + 0x6073`, the middle halfword of each new state,
 * XORed into the area halfword by halfword (POKE_CRYPTO_XorBlock and
 * POKE_CRYPTO_NextRandom, src/game/arm9/poke_tool.c).  The seed advances
 * once per halfword, and the mask is its own inverse: masking an area and
 * masking it again gives the area back.
 */
static void xor_block(u8 *data, u32 size, u32 seed)
{
    u32 halfwords = size >> 1;
    u32 state = seed;
    u32 i;

    for (i = 0; i < halfwords; i++) {
        u16 v;

        state = state * 0x41C64E6Du + 0x6073u;
        v = (u16)(state >> 16);
        data[i * 2] = (u8)(data[i * 2] ^ (u8)(v & 0xFFu));
        data[i * 2 + 1] = (u8)(data[i * 2 + 1] ^ (u8)((v >> 8) & 0xFFu));
    }
}

/*
 * The checksum of the block area: the sum of its halfwords, the way the
 * game sums them (POKE_CRYPTO_Checksum).  The halfwords are read little
 * endian, as they are in the record.
 */
static u16 block_checksum(const u8 *data, u32 size)
{
    u16 sum = 0;
    u32 halfwords = size >> 1;
    u32 i;

    for (i = 0; i < halfwords; i++) {
        u16 v = (u16)((u16)data[i * 2] | (u16)((u16)data[i * 2 + 1] << 8));

        sum = (u16)(sum + v);
    }
    return sum;
}

/*
 * The record in the clear: the two areas unmasked, the flags saying which
 * of them were in the clear already.  This is what the game does before
 * it reads a record (POKE_TOOL_DecodeCalcBlock, which unmasks the party
 * area when the party bit is clear and the block area when the box bit is
 * clear).
 */
static void record_decode(BwModMonRecord *r)
{
    if ((r->flags & BW_MOD_MON_PARTY_DECODED) == 0u) {
        xor_block(r->party, sizeof(r->party), r->personality);
        r->flags |= (u16)BW_MOD_MON_PARTY_DECODED;
    }
    if ((r->flags & BW_MOD_MON_BOX_DECODED) == 0u) {
        xor_block(r->blocks, sizeof(r->blocks), r->checksum);
        r->flags |= (u16)BW_MOD_MON_BOX_DECODED;
    }
}

/*
 * The record masked again: the two areas masked and the two decoded bits
 * cleared.  This is what the game does after it has read a record
 * (POKE_TOOL_EncodeCalcBlock), and it recomputes the checksum word, which
 * is the word the record carried.
 */
static void record_encode(BwModMonRecord *r)
{
    if ((r->flags & BW_MOD_MON_PARTY_DECODED) != 0u) {
        xor_block(r->party, sizeof(r->party), r->personality);
        r->flags &= (u16)~BW_MOD_MON_PARTY_DECODED;
    }
    if ((r->flags & BW_MOD_MON_BOX_DECODED) != 0u) {
        r->checksum = block_checksum(r->blocks, sizeof(r->blocks));
        xor_block(r->blocks, sizeof(r->blocks), r->checksum);
        r->flags &= (u16)~BW_MOD_MON_BOX_DECODED;
    }
}

/* ------------------------------------------------------------------ */
/* The port's own format                                               */
/* ------------------------------------------------------------------ */

/*
 * The header and the record, written into `buf`.  Returns the number of
 * bytes written, or -1 when the buffer is too small or an argument is
 * missing.
 */
int bw_mod_mon_pack(void *buf, u32 size, const void *record)
{
    u8 *out = (u8 *)buf;
    const u8 *rec = (const u8 *)record;

    if (out == NULL || rec == NULL || size < BW_MOD_MON_FILE_SIZE) {
        return -1;
    }
    bw_wr32(out + BW_MOD_MON_OFF_MAGIC, BW_MOD_MON_MAGIC);
    bw_wr16(out + BW_MOD_MON_OFF_VERSION, (u16)BW_MOD_MON_VERSION);
    bw_wr16(out + BW_MOD_MON_OFF_FLAGS, 0u);
    bw_wr32(out + BW_MOD_MON_OFF_SIZE, BW_MOD_MON_RECORD_SIZE);
    bw_wr32(out + BW_MOD_MON_OFF_CRC, bw_mod_mon_crc(rec, BW_MOD_MON_RECORD_SIZE));
    memcpy(out + BW_MOD_MON_HEADER_SIZE, rec, BW_MOD_MON_RECORD_SIZE);
    return (int)BW_MOD_MON_FILE_SIZE;
}

/*
 * The header and the record, read out of `buf`.  The magic, the version
 * and the checksum are checked; the record is copied out whole.  Returns 0,
 * or -1 when the buffer does not hold what it says it holds.
 */
int bw_mod_mon_unpack(const void *buf, u32 size, void *record)
{
    const u8 *in = (const u8 *)buf;
    u8 *rec = (u8 *)record;

    if (in == NULL || rec == NULL || size < BW_MOD_MON_FILE_SIZE) {
        return -1;
    }
    if (bw_rd32(in + BW_MOD_MON_OFF_MAGIC) != BW_MOD_MON_MAGIC) {
        return -1;
    }
    if (bw_rd16(in + BW_MOD_MON_OFF_VERSION) != (u16)BW_MOD_MON_VERSION) {
        return -1;
    }
    if (bw_rd32(in + BW_MOD_MON_OFF_SIZE) != BW_MOD_MON_RECORD_SIZE) {
        return -1;
    }
    if (bw_rd32(in + BW_MOD_MON_OFF_CRC) !=
        bw_mod_mon_crc(in + BW_MOD_MON_HEADER_SIZE, BW_MOD_MON_RECORD_SIZE)) {
        return -1;
    }
    memcpy(rec, in + BW_MOD_MON_HEADER_SIZE, BW_MOD_MON_RECORD_SIZE);
    return 0;
}

/*
 * The game's own record checksum: 1 when the record's block area agrees
 * with the checksum word it carries.  The check is the game's own
 * (POKE_TOOL_DecodeBoxBlock): the area is unmasked and its halfwords are
 * summed, and the sum has to be the word.
 */
int bw_mod_mon_record_ok(const void *record)
{
    BwModMonRecord copy;

    if (record == NULL) {
        return 0;
    }
    memcpy(&copy, record, sizeof(copy));
    if ((copy.flags & BW_MOD_MON_CHECKSUM_FAILED) != 0u) {
        return 0;   /* the game has already found the record broken */
    }
    record_decode(&copy);
    return block_checksum(copy.blocks, sizeof(copy.blocks)) == copy.checksum;
}

/* ------------------------------------------------------------------ */
/* The game's own accessors                                            */
/* ------------------------------------------------------------------ */

/*
 * The game's structures, named only as the opaque types its accessors
 * take.  The port never looks inside them.
 */
struct GameData;
struct Party;
struct PokemonParam;
struct BoxPokemon;
struct PersonalData;

/*
 * The game's own functions.  They are weak, so that a link without the
 * game still succeeds and a call that cannot answer says so rather than
 * calling into nothing (sdk/mod_state.c does the same).
 */
#if defined(__GNUC__) || defined(__clang__)
#define BW_PORT_WEAK __attribute__((weak))
#else
#define BW_PORT_WEAK
#endif

/* The party of the running game. */
extern void *GAME_DATA_GetMyParty(const struct GameData *gameData)
    BW_PORT_WEAK;                                        /* 0x02012934 */
extern u32 POKEPARTY_GetCount(const struct Party *party)
    BW_PORT_WEAK;                                        /* 0x0201AA18 */
extern struct PokemonParam *POKEPARTY_GetMember(struct Party *party, u32 pos)
    BW_PORT_WEAK;                                        /* 0x0201AB28 */
extern u32 POKEPARTY_Add(struct Party *party,
                         const struct PokemonParam *member)
    BW_PORT_WEAK;                                        /* 0x0201A98C */

/* The boxes. */
extern u8 BOX_SAVEDATA_GetUsableBoxCount(struct GameData *gameData)
    BW_PORT_WEAK;                                        /* 0x020076A4 */
extern struct BoxPokemon *BOX_SAVEDATA_GetPokemon(struct GameData *gameData,
                                                   u32 box, u32 slot)
    BW_PORT_WEAK;                                        /* 0x02007A18 */
extern int BOX_SAVEDATA_AddPokemonToBox(struct GameData *gameData, u32 box,
                                        const struct BoxPokemon *mon)
    BW_PORT_WEAK;                                        /* 0x0200752C */
extern int BOX_SAVEDATA_FindFreeSlot(struct GameData *gameData, int *pBox,
                                     int *pSlot)
    BW_PORT_WEAK;                                        /* 0x020076D0 */

/* A Pokemon's own fields. */
extern u32 POKE_TOOL_GetPartyValue(struct PokemonParam *mon, u32 paramId,
                                   u32 arg)
    BW_PORT_WEAK;                                        /* 0x02017E1C */

/* The species' own record, and the move's own record. */
extern struct PersonalData *POKE_TOOL_GetPersonalData(u32 species, u16 form)
    BW_PORT_WEAK;                                        /* 0x02019C54 */
extern u32 POKE_PERSONAL_GetParam(const struct PersonalData *data, u32 field)
    BW_PORT_WEAK;                                        /* 0x0201AE1C */
extern u32 WAZA_TOOL_GetParam(u32 move, u32 param)
    BW_PORT_WEAK;                                        /* 0x0201BD28 */

/* ------------------------------------------------------------------ */
/* The port's own seam                                                 */
/* ------------------------------------------------------------------ */

/*
 * The game data of the running game.  The state layer (sdk/mod_state.c)
 * binds the same object for the state calls; this file needs it for the
 * raw records, which the state calls do not carry, so the port binds it
 * here as well, wherever it binds it there.
 */
static void *g_game_data;

void bw_mod_export_set_game_data(void *game_data)
{
    g_game_data = game_data;
}

/*
 * Forget the binding: at power-on and at every reset (bw_port_reset).
 */
void bw_mod_export_reset(void)
{
    g_game_data = NULL;
}

/* The party of the running game, or NULL when there is none. */
static struct Party *live_party(void)
{
    if (GAME_DATA_GetMyParty == NULL || g_game_data == NULL) {
        return NULL;
    }
    return (struct Party *)GAME_DATA_GetMyParty(
        (const struct GameData *)g_game_data);
}

/* A party member, or NULL when the slot is empty or past the party. */
static struct PokemonParam *live_member(u32 slot)
{
    struct Party *party = live_party();

    if (party == NULL || POKEPARTY_GetCount == NULL ||
        POKEPARTY_GetMember == NULL) {
        return NULL;
    }
    if (slot >= POKEPARTY_GetCount(party)) {
        return NULL;
    }
    return POKEPARTY_GetMember(party, slot);
}

/*
 * A box record, or NULL.  The game's own accessor fails the game when the
 * box or the slot is out of range, so the bounds are checked here: the
 * usable box count (the header's own byte) and the box's own capacity of
 * thirty slots (src/game/arm9/box_savedata.c).
 */
#define BW_MOD_MON_BOX_CAPACITY 30u

static struct BoxPokemon *live_box_pokemon(u32 box, u32 slot)
{
    u32 boxes;

    if (BOX_SAVEDATA_GetUsableBoxCount == NULL ||
        BOX_SAVEDATA_GetPokemon == NULL || g_game_data == NULL) {
        return NULL;
    }
    boxes = (u32)BOX_SAVEDATA_GetUsableBoxCount(
        (struct GameData *)g_game_data);
    if (box >= boxes || slot >= BW_MOD_MON_BOX_CAPACITY) {
        return NULL;
    }
    return BOX_SAVEDATA_GetPokemon((struct GameData *)g_game_data, box, slot);
}

/* ------------------------------------------------------------------ */
/* The export                                                          */
/* ------------------------------------------------------------------ */

/*
 * The record of the Pokemon the reference names, in the port's own
 * spelling.  A party member is taken as it is; a box record is the box
 * part of the record, and the party part is empty and masked the way the
 * game masks a cleared record (POKE_TOOL_ClearPartyParam).
 */
static int record_of(BwMonRef mon, BwModMonRecord *out)
{
    if (mon.place == (u8)BW_MON_PARTY) {
        struct PokemonParam *member = live_member(mon.slot);

        if (member == NULL) {
            return 0;
        }
        memcpy(out, member, sizeof(*out));
        return 1;
    }
    if (mon.place == (u8)BW_MON_BOX) {
        struct BoxPokemon *box = live_box_pokemon(mon.box, mon.slot);

        if (box == NULL) {
            return 0;
        }
        memset(out, 0, sizeof(*out));
        memcpy(out, box, BW_MOD_MON_BOX_SIZE);
        /*
         * The party part is empty, and it is in the state the box part is
         * in: the game sets the two decoded bits together and clears them
         * together, so a record whose two bits differ is not a record the
         * game made.  The party area is masked with the personality the
         * way the game masks a cleared record, or it is in the clear.
         */
        if ((out->flags & BW_MOD_MON_BOX_DECODED) != 0u) {
            out->flags |= (u16)BW_MOD_MON_PARTY_DECODED;
            memset(out->party, 0, sizeof(out->party));
        } else {
            out->flags &= (u16)~BW_MOD_MON_PARTY_DECODED;
            xor_block(out->party, sizeof(out->party), out->personality);
        }
        return 1;
    }
    return 0;
}

/*
 * The `mon_export` call of the services table: the Pokemon's record, with
 * the port's header in front of it, written into the caller's buffer.
 * Returns the number of bytes written, or -1.
 */
int bw_mod_mon_export(BwMonRef mon, void *buf, u32 size)
{
    BwModMonRecord record;

    if (buf == NULL || size < BW_MOD_MON_FILE_SIZE) {
        return -1;
    }
    if (!record_of(mon, &record)) {
        return -1;
    }
    if (!bw_mod_mon_record_ok(&record)) {
        return -1;   /* the record is broken: the game would not read it */
    }
    return bw_mod_mon_pack(buf, size, &record);
}

/* ------------------------------------------------------------------ */
/* The import                                                          */
/* ------------------------------------------------------------------ */

/*
 * Does the game know the species?  The personal record's base HP is not
 * zero for a species the game has, and the species is one of the game's
 * own (the table ends at Arceus).
 */
static int species_known(u32 species)
{
    const struct PersonalData *personal;

    if (species == 0u || species > BW_MOD_MON_MAX_SPECIES) {
        return 0;
    }
    if (POKE_TOOL_GetPersonalData == NULL || POKE_PERSONAL_GetParam == NULL) {
        return 0;
    }
    personal = POKE_TOOL_GetPersonalData(species, 0);
    if (personal == NULL) {
        return 0;
    }
    return POKE_PERSONAL_GetParam(personal, BW_MOD_MON_PERSONAL_BASE_HP) != 0u;
}

/*
 * Does the game know the move?  The move record's base PP is not zero for
 * a move the game has, and a move the game does not have reads as an empty
 * record.
 */
static int move_known(u32 move)
{
    if (move == 0u) {
        return 1;   /* a move slot may be empty */
    }
    if (WAZA_TOOL_GetParam == NULL) {
        return 0;
    }
    return WAZA_TOOL_GetParam(move, BW_MOD_MON_WAZA_BASE_PP) != 0u;
}

/*
 * Does the game know the item?  The item table ends at 626, and the
 * game's own accessor refuses any item above it.
 */
static int item_known(u32 item)
{
    return item <= BW_MOD_MON_MAX_ITEM;
}

/*
 * The item the record carries.
 *
 * The game's own accessor answers 0 for an item above 626: it refuses the
 * value, so the port cannot see an item the game does not know through it.
 * The port finds the item's own two bytes instead: the item is at +0x02 of
 * the block the species is in, and the block is found by what it holds (the
 * species, the original trainer's id and the experience), which the game's
 * accessors hand out without clamping any of them.
 *
 * Returns 0 when the record does not answer to that: a record whose blocks
 * are not the game's own is not a record the game made.
 */
static int raw_item(const BwModMonRecord *record, u32 species, u32 ot_id,
                    u32 exp, u32 *item)
{
    BwModMonRecord copy;
    u32 i;
    int found = -1;

    memcpy(&copy, record, sizeof(copy));
    record_decode(&copy);
    for (i = 0; i < BW_MOD_MON_BLOCK_COUNT; i++) {
        const u8 *block = copy.blocks + i * BW_MOD_MON_BLOCK_SIZE;

        if (bw_rd16(block) != (u16)species) {
            continue;
        }
        if (bw_rd32(block + 4) != ot_id) {
            continue;
        }
        if (bw_rd32(block + 8) != exp) {
            continue;
        }
        if (found >= 0) {
            return 0;   /* two blocks answer: the record is not the game's */
        }
        found = (int)i;
    }
    if (found < 0) {
        return 0;
    }
    *item = bw_rd16(copy.blocks + (u32)found * BW_MOD_MON_BLOCK_SIZE + 2);
    return 1;
}

/*
 * Put the record into the party, and into a box when the party is full.
 * The reference the Pokemon ended up at is written to `out`.
 */
static int place_record(BwModMonRecord *record, BwMonRef *out)
{
    struct Party *party = live_party();

    if (party != NULL && POKEPARTY_Add != NULL && POKEPARTY_GetCount != NULL) {
        u32 count = POKEPARTY_GetCount(party);

        if (POKEPARTY_Add(party, (const struct PokemonParam *)record) != 0u) {
            out->handle = ((u32)BW_MON_PARTY << 24) | (u32)count;
            out->place = (u8)BW_MON_PARTY;
            out->side = 0;
            out->box = 0;
            out->slot = (u8)count;
            return 0;
        }
    }
    /* The party is full: the boxes take it. */
    if (BOX_SAVEDATA_FindFreeSlot == NULL ||
        BOX_SAVEDATA_AddPokemonToBox == NULL || g_game_data == NULL) {
        return -1;
    }
    {
        int box = -1;
        int slot = 0;

        if (BOX_SAVEDATA_FindFreeSlot((struct GameData *)g_game_data, &box,
                                      &slot) == 0) {
            return -1;   /* every box is full */
        }
        if (BOX_SAVEDATA_AddPokemonToBox((struct GameData *)g_game_data,
                                         (u32)box,
                                         (const struct BoxPokemon *)record)
            == 0) {
            return -1;
        }
        out->handle = ((u32)BW_MON_BOX << 24) | ((u32)box << 8) |
                      (u32)slot;
        out->place = (u8)BW_MON_BOX;
        out->side = 0;
        out->box = (u8)box;
        out->slot = (u8)slot;
        return 0;
    }
}

/*
 * The `mon_import` call of the services table: the Pokemon in the file
 * goes into the party, or into a box when the party is full.  The
 * reference it ended up at is written to `out`.  Returns 0, or -1.
 *
 * The checks are the ones the document names: the port's own header, the
 * game's own record checksum, and the species, the moves and the item the
 * game has to know.
 */
int bw_mod_mon_import(const void *buf, u32 size, BwMonRef *out)
{
    BwModMonRecord record;
    u32 species;
    u32 ot_id;
    u32 exp;
    u32 item;
    u32 raw;
    u32 moves[4];
    u32 i;

    if (buf == NULL || out == NULL) {
        return -1;
    }
    if (bw_mod_mon_unpack(buf, size, &record) != 0) {
        return -1;   /* the port's own header */
    }
    if (!bw_mod_mon_record_ok(&record)) {
        return -1;   /* the game's own record checksum */
    }
    if (POKE_TOOL_GetPartyValue == NULL) {
        return -1;   /* there is no game to read the record */
    }
    /*
     * The game's own accessor unmasks the record, reads the field and
     * masks the record again, so the record is read in the state it is in.
     */
    species = POKE_TOOL_GetPartyValue((struct PokemonParam *)&record,
                                       BW_MON_FIELD_SPECIES, 0);
    ot_id = POKE_TOOL_GetPartyValue((struct PokemonParam *)&record,
                                    BW_MOD_MON_FIELD_OT_ID, 0);
    exp = POKE_TOOL_GetPartyValue((struct PokemonParam *)&record,
                                  BW_MON_FIELD_EXP, 0);
    item = POKE_TOOL_GetPartyValue((struct PokemonParam *)&record,
                                   BW_MON_FIELD_ITEM, 0);
    for (i = 0; i < 4; i++) {
        moves[i] = POKE_TOOL_GetPartyValue((struct PokemonParam *)&record,
                                           BW_MOD_MON_FIELD_MOVE + i, 0);
    }
    if (!species_known(species)) {
        return -1;
    }
    for (i = 0; i < 4; i++) {
        if (!move_known(moves[i])) {
            return -1;
        }
    }
    /*
     * The item: the game's own accessor answers 0 for an item above 626,
     * so the port looks for the item's own bytes in the record and checks
     * those when the record gives them up.  A record whose blocks do not
     * answer to that is not a record the game made, and the item the
     * accessor gave is the one the port goes by.
     */
    if (raw_item(&record, species, ot_id, exp, &raw) != 0) {
        item = raw;
    }
    if (!item_known(item)) {
        return -1;
    }
    /* The record goes to the game in the state the game keeps its own
     * records in: masked. */
    record_encode(&record);
    return place_record(&record, out);
}

/* ------------------------------------------------------------------ */
/* The port's own calls                                                */
/* ------------------------------------------------------------------ */

/*
 * The port's own calls below are not part of the mod ABI.  A mod reaches
 * the service through the services table (sdk/mod_state.h) and the loader
 * binds the two calls above into it.
 */

/*
 * The exported Pokemon, written to a file: the whole file, atomically
 * (bw_save_write_atomic, sdk/save.h), so a crash leaves the old file or
 * the new one and never a torn one.  This is the "from one save to a
 * file" half of the service; a Genlocke carries the file into the next
 * game.
 */
int bw_mod_mon_export_file(BwMonRef mon, const char *path)
{
    u8 buf[BW_MOD_MON_FILE_SIZE];
    int n;

    if (path == NULL || path[0] == '\0') {
        return -1;
    }
    n = bw_mod_mon_export(mon, buf, sizeof(buf));
    if (n < 0) {
        return -1;
    }
    return bw_save_write_atomic(path, buf, (u32)n);
}

/*
 * The Pokemon in a file, imported: the "from a file to another save" half
 * of the service.  The file is read whole and the record is checked as it
 * is for any other import.
 */
int bw_mod_mon_import_file(const char *path, BwMonRef *out)
{
    u8 buf[BW_MOD_MON_FILE_SIZE];
    long n;

    if (path == NULL || path[0] == '\0' || out == NULL) {
        return -1;
    }
    n = bw_save_read(path, buf, sizeof(buf));
    if (n < (long)sizeof(buf)) {
        return -1;
    }
    return bw_mod_mon_import(buf, sizeof(buf), out);
}
