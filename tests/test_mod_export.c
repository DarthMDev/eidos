/*
 * tests/test_mod_export.c -- the export and the import of a Pokemon
 * (docs/mod-services.md, "Pokemon export and import"; sdk/mod_export.c).
 *
 * A Pokemon moves between games: a Genlocke carries the record into the
 * next game, Wonderlocke and Egglocke send it through a peer session.
 * The exported form is the game's own record, 0xDC bytes, with the
 * port's header in front of it: a magic, a format version and a
 * checksum.
 *
 * What is checked here:
 *
 *   - the exported form: the port's header is in front, it carries the
 *     magic, the format version, the size of the record and the
 *     checksum, and the game's own record is behind it, byte for byte;
 *   - a round trip: what comes out of the exported form is the record
 *     that went in, and the import gives the game that Pokemon;
 *   - a form with a broken checksum is refused, and so is a record whose
 *     own checksum (the game's own rule) does not match;
 *   - a record whose species the game does not know is refused;
 *   - a call that reaches the game without the game there fails rather
 *     than reading nothing.
 *
 * The service reaches the game's records through the game's own
 * accessors, which the port declares weak, so that a build without the
 * game still links.  This test supplies them: the party holds one
 * Pokemon, and the game knows the first 493 species.
 *
 * What this test does not cover: the boxes an import falls back to when
 * the party is full (the box accessors are the game's own), and the
 * import from and the export to a file (the port's own file calls).
 */

#include <stdio.h>
#include <string.h>

#include "mod_api.h"
#include "mod_export.h"
#include "mod_state.h"
#include "port_test.h"
#include "types.h"

/* ------------------------------------------------------------------ */
/* The port's own calls                                                */
/* ------------------------------------------------------------------ */

/*
 * The calls of sdk/mod_export.c (sdk/mod_export.h).  The first two are
 * the ones the loader binds into the services table (sdk/mod_state.h);
 * the rest are the port's own.
 *
 * The exported form's own numbers are the file's, and the header does not
 * carry them: the test keeps its own copies, so a change in the file's
 * format shows up here as a failure.
 */
#define T_MAGIC       0x4D505742u   /* "BWPM", little-endian */
#define T_VERSION     1u
#define T_HEADER_SIZE 16u
#define T_RECORD_SIZE 0xDCu
#define T_FILE_SIZE   (T_HEADER_SIZE + T_RECORD_SIZE)

/* The offsets of the header's fields. */
#define T_OFF_MAGIC   0u
#define T_OFF_VERSION 4u
#define T_OFF_FLAGS   6u
#define T_OFF_SIZE    8u
#define T_OFF_CRC     12u

/* The field ids the game's own accessors take. */
#define T_FIELD_SPECIES 0x05u
#define T_FIELD_ITEM    0x06u

/* The last species the game knows (Arceus). */
#define T_LAST_SPECIES 493u

/* ------------------------------------------------------------------ */
/* The record                                                         */
/* ------------------------------------------------------------------ */

/*
 * The game's own Pokemon record, in the test's own spelling: the same
 * 0xDC bytes the game keeps (src/game/arm9/poke_tool_types.h), a box
 * part and a party part.
 */
typedef struct TestRecord {
    u32 personality;
    u16 flags;
    u16 checksum;
    u8  blocks[0x80];
    u8  party[0x54];
} TestRecord;

/* The record is 0xDC bytes, the size the export carries. */
typedef char test_record_is_dc[(sizeof(TestRecord) == T_RECORD_SIZE) ? 1 : -1];

/* The flags of the record: the two decoded bits, and the game's own
 * "the checksum did not match" bit. */
#define T_PARTY_DECODED   0x0001u
#define T_BOX_DECODED     0x0002u
#define T_CHECKSUM_FAILED 0x0004u

/*
 * The game's own checksum of the block area: the sum of its halfwords,
 * read little-endian (POKE_CRYPTO_Checksum).
 */
static u16 block_checksum(const u8 *data, u32 size)
{
    u16 sum = 0;
    u32 i;

    for (i = 0; i + 1u < size; i += 2u) {
        sum = (u16)(sum + (u16)((u16)data[i] | ((u16)data[i + 1u] << 8)));
    }
    return sum;
}

/*
 * Builds a record the game can read: the record in the clear (both
 * decoded bits set) and the game's own checksum over the block area.
 */
static void build_record(TestRecord *r, u32 personality)
{
    u32 i;

    memset(r, 0, sizeof(*r));
    r->personality = personality;
    r->flags = (u16)(T_PARTY_DECODED | T_BOX_DECODED);
    for (i = 0; i < sizeof(r->blocks); i++) {
        r->blocks[i] = (u8)(i * 7u + 1u);
    }
    r->checksum = block_checksum(r->blocks, sizeof(r->blocks));
}

/* ------------------------------------------------------------------ */
/* The game of the test                                               */
/* ------------------------------------------------------------------ */

/* The game's own types, named only as the opaque types its accessors
 * take (the port never looks inside them). */
struct GameData;
struct Party;
struct PokemonParam;
struct PersonalData;

/* The Pokemon the fake party holds, and the one it was given. */
static TestRecord g_record;
static TestRecord g_added;
static int g_added_times;

/* The species the fake record holds. */
static u32 g_species = 25;

/* The fake game's personal record and its party. */
static int g_personal;
static int g_party;

/* The game data the port is given. */
static int g_game_data;

/* The party's Pokemon: the fake party holds one. */
void *GAME_DATA_GetMyParty(const struct GameData *gameData)
{
    (void)gameData;
    return &g_party;
}

u32 POKEPARTY_GetCount(const struct Party *party)
{
    (void)party;
    return 1u;
}

struct PokemonParam *POKEPARTY_GetMember(struct Party *party, u32 pos)
{
    (void)party;
    if (pos != 0u) {
        return NULL;
    }
    return (struct PokemonParam *)&g_record;
}

u32 POKEPARTY_Add(struct Party *party, const struct PokemonParam *member)
{
    (void)party;
    memcpy(&g_added, member, sizeof(g_added));
    g_added_times++;
    return 1u;   /* the game took it */
}

/* A field of the Pokemon: the species, and nothing else (no item, no
 * moves). */
u32 POKE_TOOL_GetPartyValue(struct PokemonParam *mon, u32 paramId, u32 arg)
{
    (void)mon;
    (void)arg;
    if (paramId == T_FIELD_SPECIES) {
        return g_species;
    }
    if (paramId == T_FIELD_ITEM) {
        return 0u;   /* no item */
    }
    return 0u;       /* no moves */
}

/* The personal record of a species the game knows. */
struct PersonalData *POKE_TOOL_GetPersonalData(u32 species, u16 form)
{
    (void)form;
    if (species == 0u || species > T_LAST_SPECIES) {
        return NULL;
    }
    return (struct PersonalData *)&g_personal;
}

/* A field of the personal record: the base hp, which is not zero for a
 * species the game has. */
u32 POKE_PERSONAL_GetParam(const struct PersonalData *data, u32 field)
{
    (void)data;
    (void)field;
    return 1u;
}

/* A field of a move's record: the base pp, which is not zero for a move
 * the game has. */
u32 WAZA_TOOL_GetParam(u32 move, u32 param)
{
    (void)move;
    (void)param;
    return 1u;
}

/* ------------------------------------------------------------------ */
/* The test                                                            */
/* ------------------------------------------------------------------ */

int main(void)
{
    BwMonRef mon;
    BwMonRef out;
    TestRecord unpacked;
    u8 buf[T_FILE_SIZE];
    u8 broken[T_FILE_SIZE];
    int written;
    int rc;

    printf("the exported form\n");
    build_record(&g_record, 0x12345678u);
    memset(&g_added, 0, sizeof(g_added));
    g_added_times = 0;
    g_species = 25;

    mon.handle = 0;
    mon.place = (u8)BW_MON_PARTY;
    mon.side = 0;
    mon.box = 0;
    mon.slot = 0;

    bw_mod_export_set_game_data(&g_game_data);

    memset(buf, 0, sizeof(buf));
    written = bw_mod_mon_export(mon, buf, (u32)sizeof(buf));
    BW_REQUIRE("the export writes the form", written > 0);
    BW_CHECK_EQ("the form is the header and the record",
                (long)written, (long)T_FILE_SIZE);

    BW_CHECK_EQ("the header carries the magic",
                (long)bw_rd32(buf + T_OFF_MAGIC), (long)T_MAGIC);
    BW_CHECK_EQ("the magic spells BWPM",
                (long)(buf[0] == 'B' && buf[1] == 'W' &&
                       buf[2] == 'P' && buf[3] == 'M'), 1);
    BW_CHECK_EQ("the header carries the format version",
                (long)bw_rd16(buf + T_OFF_VERSION), (long)T_VERSION);
    BW_CHECK_EQ("the header carries no flags",
                (long)bw_rd16(buf + T_OFF_FLAGS), 0);
    BW_CHECK_EQ("the header carries the size of the record",
                (long)bw_rd32(buf + T_OFF_SIZE), (long)T_RECORD_SIZE);
    BW_CHECK_EQ("the header carries the checksum of the record",
                (long)bw_rd32(buf + T_OFF_CRC),
                (long)bw_mod_mon_crc(&g_record, T_RECORD_SIZE));
    BW_CHECK_EQ("the game's own record is behind the header",
                memcmp(buf + T_HEADER_SIZE, &g_record, T_RECORD_SIZE), 0);

    printf("a round trip\n");
    memset(&unpacked, 0, sizeof(unpacked));
    rc = bw_mod_mon_unpack(buf, (u32)written, &unpacked);
    BW_CHECK_EQ("the form is read back", (long)rc, 0);
    BW_CHECK_EQ("and it is the record that went in",
                memcmp(&unpacked, &g_record, T_RECORD_SIZE), 0);

    out.handle = 0xFFFFFFFFu;
    out.place = 0xFFu;
    out.side = 0xFFu;
    out.box = 0xFFu;
    out.slot = 0xFFu;
    rc = bw_mod_mon_import(buf, (u32)written, &out);
    BW_CHECK_EQ("the import takes the form", (long)rc, 0);
    BW_CHECK_EQ("the game was given the Pokemon once", g_added_times, 1);
    BW_CHECK_EQ("the Pokemon went into the party",
                (long)out.place, (long)BW_MON_PARTY);
    BW_CHECK_EQ("the game was given the same Pokemon",
                (long)(g_added.personality == g_record.personality), 1);
    BW_CHECK_EQ("with the same checksum of its own",
                (long)(g_added.checksum == g_record.checksum), 1);
    BW_CHECK_EQ("and the game can read it",
                (long)bw_mod_mon_record_ok(&g_added), 1);

    printf("a broken checksum\n");
    memcpy(broken, buf, (size_t)written);
    broken[T_HEADER_SIZE + 3] ^= 0xFFu;
    memset(&unpacked, 0, sizeof(unpacked));
    rc = bw_mod_mon_unpack(broken, (u32)written, &unpacked);
    BW_CHECK_EQ("the form is refused", (long)(rc < 0), 1);
    g_added_times = 0;
    rc = bw_mod_mon_import(broken, (u32)written, &out);
    BW_CHECK_EQ("the import refuses it", (long)(rc < 0), 1);
    BW_CHECK_EQ("and the game is given nothing", g_added_times, 0);

    printf("a wrong magic\n");
    memcpy(broken, buf, (size_t)written);
    broken[T_OFF_MAGIC] ^= 0xFFu;
    rc = bw_mod_mon_import(broken, (u32)written, &out);
    BW_CHECK_EQ("the import refuses it", (long)(rc < 0), 1);

    printf("a form that is too short\n");
    rc = bw_mod_mon_import(buf, (u32)(T_HEADER_SIZE - 1u), &out);
    BW_CHECK_EQ("the import refuses it", (long)(rc < 0), 1);

    printf("a record the game cannot read\n");
    g_record.checksum = (u16)(g_record.checksum + 1u);
    BW_CHECK_EQ("the port's own check sees it",
                (long)bw_mod_mon_record_ok(&g_record), 0);
    rc = bw_mod_mon_export(mon, buf, (u32)sizeof(buf));
    BW_CHECK_EQ("the export refuses it", (long)(rc < 0), 1);
    /* The form itself is sound; the record inside it is the broken one. */
    written = bw_mod_mon_pack(buf, (u32)sizeof(buf), &g_record);
    BW_REQUIRE("the form is written", written > 0);
    g_added_times = 0;
    rc = bw_mod_mon_import(buf, (u32)written, &out);
    BW_CHECK_EQ("the import refuses it", (long)(rc < 0), 1);
    BW_CHECK_EQ("and the game is given nothing", g_added_times, 0);
    build_record(&g_record, 0x12345678u);

    printf("a record the game has already found broken\n");
    g_record.flags = (u16)(g_record.flags | T_CHECKSUM_FAILED);
    BW_CHECK_EQ("the port's own check sees it",
                (long)bw_mod_mon_record_ok(&g_record), 0);
    written = bw_mod_mon_pack(buf, (u32)sizeof(buf), &g_record);
    BW_REQUIRE("the form is written", written > 0);
    g_added_times = 0;
    rc = bw_mod_mon_import(buf, (u32)written, &out);
    BW_CHECK_EQ("the import refuses it", (long)(rc < 0), 1);
    BW_CHECK_EQ("and the game is given nothing", g_added_times, 0);
    build_record(&g_record, 0x12345678u);

    printf("a species the game does not know\n");
    g_species = T_LAST_SPECIES + 1u;
    written = bw_mod_mon_pack(buf, (u32)sizeof(buf), &g_record);
    BW_REQUIRE("the form is written", written > 0);
    g_added_times = 0;
    rc = bw_mod_mon_import(buf, (u32)written, &out);
    BW_CHECK_EQ("the import refuses it", (long)(rc < 0), 1);
    BW_CHECK_EQ("and the game is given nothing", g_added_times, 0);

    printf("a species of no kind at all\n");
    g_species = 0;
    written = bw_mod_mon_pack(buf, (u32)sizeof(buf), &g_record);
    BW_REQUIRE("the form is written", written > 0);
    rc = bw_mod_mon_import(buf, (u32)written, &out);
    BW_CHECK_EQ("the import refuses it", (long)(rc < 0), 1);
    g_species = 25;

    printf("no game\n");
    bw_mod_export_reset();
    rc = bw_mod_mon_export(mon, buf, (u32)sizeof(buf));
    BW_CHECK_EQ("the export fails without the game", (long)(rc < 0), 1);
    bw_mod_export_set_game_data(&g_game_data);

    BW_REPORT("test_mod_export");
}
