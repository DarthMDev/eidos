/*
 * sdk/arm7/card.c -- the ARM7's Slot-1 card module, PXI tag 14.
 *
 * Why a stand-in: sdk/arm7/arm7.h.
 *
 * On tag 14 the ARM7 tells the ARM9 about the card slot:
 * CARDi_PulledOutCallback (src/matched/arm9_sdk/func_02077410.c) reads
 * command 0x11 as "the card was pulled out" and 2 as "the slot was reset",
 * and terminates on anything else.  The host's card cannot be pulled out,
 * so the ARM7 never sends either.  Words the ARM9 sends on this tag
 * (func_02077568.c) are taken and need no reply.
 *
 * The ARM7 still registers the tag, as its firmware does, so
 * PXI_IsCallbackReady(14, 1) is true.
 */

#include "arm7.h"
#include "dsmem.h"
#include "pxi.h"

static void card_callback(int tag, u32 data, int err)
{
    (void)tag;
    (void)data;
    (void)err;
}

void bw_arm7_card_init(void)
{
    bw_pxi_arm7_set_callback(BW_PXI_TAG_CARD, card_callback);
}

/*
 * The card's SPI bus (AUXSPICNT 0x040001A0, AUXSPIDATA 0x040001A2), as far
 * as the game uses it outside the backup library: Black and White's cards
 * carry an infrared chip on that bus, and the game asks it for its id
 * (ov231's CARD_SPI_CheckCardId: command 0x08, then a byte read back that a
 * real card answers with 0xAA).  The battle turns experience off when the
 * answer is wrong (BTL_MAIN_CheckEventOverlayAllowed), which is what a
 * copied cartridge without the chip gets.  The port's card is the
 * player's own dump of a real one, so the bus answers as the real one does.
 *
 * A store to AUXSPIDATA is one byte out on the bus, and the byte the
 * device sends back is what the next read finds there.  The access hook
 * (dsmem.h) runs on every translation of the address, so a store is seen
 * at the next access as a register value different from the one the bus
 * last showed: then the reply goes in its place, before that access reads
 * it.
 */
#define AUXSPIDATA      0x040001A2u
#define IR_CMD_ID       0x08u
#define IR_ID           0xAAu
#define SPI_IDLE        0xFFu

static u16 g_spi_shown;     /* what the bus last put in AUXSPIDATA        */
static int g_spi_after_id;  /* the last byte out was the id command       */

static void spi_access(u32 addr, int sync)
{
    u16 v = bw_io_read16(AUXSPIDATA);
    u16 reply;

    (void)addr;
    (void)sync;
    if (v == g_spi_shown) {
        return;             /* no byte went out since the last reply */
    }
    if (g_spi_after_id) {
        reply = IR_ID;
        g_spi_after_id = 0;
    } else {
        reply = SPI_IDLE;
        g_spi_after_id = (v & 0xFFu) == IR_CMD_ID;
    }
    g_spi_shown = reply;
    bw_io_write16(AUXSPIDATA, reply);
}

void bw_card_spi_reset(void)
{
    g_spi_shown = 0;
    g_spi_after_id = 0;
    bw_io_access_hook(AUXSPIDATA, AUXSPIDATA + 2u, spi_access);
}
