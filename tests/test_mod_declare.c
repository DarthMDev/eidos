/*
 * tests/test_mod_declare.c -- the declared set at run time.
 *
 * The set is mod/declared_set.txt: the (region, address) pairs a mod may
 * override (docs/mod-interface.md, section 1).  The build emits the whole
 * set as one table beside the slot publications and registers it at
 * start-up, so the manifest's check answers for every region whether or
 * not that region has loaded (docs/mod-state.md, section 4).
 *
 * What is checked here:
 *
 *   - a declared pair answers "declared";
 *   - an undeclared pair does not;
 *   - the Thumb-tagged form of a declared address answers the same as the
 *     even start (the low bit is masked before the lookup, so the value a
 *     code pointer holds names the same function);
 *   - the same address in another region does not answer: the key is the
 *     pair, never the address alone (two overlays share one RAM window,
 *     so one address names two functions);
 *   - a table registered in any order answers the same: the set is kept
 *     sorted by region and address and searched by bisection;
 *   - a second table replaces the first, and an empty table leaves
 *     nothing declared.
 *
 * The table is the test's own, a handful of rows of the real set: the
 * table the build emits lives in the boot link (tools/bootlink.py,
 * write_link_c), which is not in the library a test links against, and
 * the set's contents are aletheia's, not the port's.  What the port owns
 * is the answer, and that is what this test drives.
 *
 * Nothing here needs game data.
 */

#include <stdio.h>

#include "mod_slot.h"
#include "port_test.h"

/* The rows the test registers.  They are rows of the real set, and the
 * table is deliberately not in the order the set is sorted in. */
#define T_ARM9_ADDR   0x02007150u
#define T_OVL93_ADDR  0x021CBA40u
#define T_SHARED_ADDR 0x021C5200u

/* The region the shared address belongs to here. */
#define T_OVL93_REGION  BW_MOD_REGION_OVL(93)

/* Another region: the same address, another overlay (ov208 shares the
 * window with ov093). */
#define T_OTHER_REGION  BW_MOD_REGION_OVL(208)

int main(void)
{
    static const BwModDeclared k_rows[] = {
        { BW_MOD_REGION_ARM9,   T_ARM9_ADDR   },
        { T_OVL93_REGION,       T_SHARED_ADDR },
        { T_OVL93_REGION,       T_OVL93_ADDR  }
    };

    printf("the declared set\n");
    bw_mod_declare_table(k_rows, (unsigned int)(sizeof(k_rows) / sizeof(k_rows[0])));

    BW_CHECK_EQ("a declared pair answers", bw_mod_slot_declared(BW_MOD_REGION_ARM9,
                                                               T_ARM9_ADDR), 1);
    BW_CHECK_EQ("a declared overlay pair answers",
                bw_mod_slot_declared(T_OVL93_REGION, T_OVL93_ADDR), 1);
    BW_CHECK_EQ("an undeclared pair does not",
                bw_mod_slot_declared(BW_MOD_REGION_ARM9, 0x02007160u), 0);
    BW_CHECK_EQ("and an undeclared overlay pair does not",
                bw_mod_slot_declared(T_OVL93_REGION, 0x021CBA60u), 0);

    printf("the Thumb-tagged form\n");
    /* The set holds the even start; a code pointer holds the Thumb-tagged
     * value, and the low bit says which (docs/mod-state.md, section 4). */
    BW_CHECK_EQ("the tagged form answers the same as the even start",
                bw_mod_slot_declared(BW_MOD_REGION_ARM9, T_ARM9_ADDR | 1u),
                bw_mod_slot_declared(BW_MOD_REGION_ARM9, T_ARM9_ADDR));
    BW_CHECK_EQ("and it is declared",
                bw_mod_slot_declared(BW_MOD_REGION_ARM9, T_ARM9_ADDR | 1u), 1);
    BW_CHECK_EQ("the tagged form of an undeclared address is not declared",
                bw_mod_slot_declared(BW_MOD_REGION_ARM9, 0x02007160u | 1u), 0);

    printf("the same address in another region\n");
    /* The key is the pair, never the address alone: two overlays share one
     * RAM window, so one address names two functions (sdk/mod_slot.h). */
    BW_CHECK_EQ("the address in its own region answers",
                bw_mod_slot_declared(T_OVL93_REGION, T_SHARED_ADDR), 1);
    BW_CHECK_EQ("the same address in another region does not",
                bw_mod_slot_declared(T_OTHER_REGION, T_SHARED_ADDR), 0);
    BW_CHECK_EQ("and in the arm9 it does not",
                bw_mod_slot_declared(BW_MOD_REGION_ARM9, T_SHARED_ADDR), 0);

    printf("the set is searched by bisection\n");
    /* The rows above are not in the order the set is sorted in, and every
     * answer above was right anyway: the set is sorted when it is
     * registered (sdk/mod_slot.c). */
    BW_CHECK_EQ("the last row answers",
                bw_mod_slot_declared(T_OVL93_REGION, T_OVL93_ADDR), 1);
    BW_CHECK_EQ("the first row answers",
                bw_mod_slot_declared(BW_MOD_REGION_ARM9, T_ARM9_ADDR), 1);
    /* The rows around the one that is asked for: a bisection that stopped
     * early would answer 0 for a row that is there. */
    BW_CHECK_EQ("a row above the first one answers",
                bw_mod_slot_declared(T_OVL93_REGION, T_SHARED_ADDR), 1);
    BW_CHECK_EQ("a row below the last one does not",
                bw_mod_slot_declared(T_OVL93_REGION, 0x021CBA60u), 0);

    printf("a second table replaces the first\n");
    {
        static const BwModDeclared k_other[] = {
            { BW_MOD_REGION_ARM9, 0x02008A7Cu }
        };

        bw_mod_declare_table(k_other, 1u);
        BW_CHECK_EQ("the new row answers",
                    bw_mod_slot_declared(BW_MOD_REGION_ARM9, 0x02008A7Cu), 1);
        BW_CHECK_EQ("a row of the old table does not",
                    bw_mod_slot_declared(BW_MOD_REGION_ARM9, T_ARM9_ADDR), 0);
        BW_CHECK_EQ("and neither does the overlay's",
                    bw_mod_slot_declared(T_OVL93_REGION, T_OVL93_ADDR), 0);
    }

    printf("an empty table\n");
    bw_mod_declare_table(NULL, 0u);
    BW_CHECK_EQ("nothing is declared",
                bw_mod_slot_declared(BW_MOD_REGION_ARM9, 0x02008A7Cu), 0);

    BW_REPORT("test_mod_declare");
}
