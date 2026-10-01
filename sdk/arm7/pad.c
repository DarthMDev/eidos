/*
 * sdk/arm7/pad.c -- the ARM7's half of the keypad: X, Y, the debug button
 * and the hinge, published in HW_BUTTON_XY_BUF.
 *
 * Why a stand-in: sdk/arm7/arm7.h.  X, Y, DEBUG and the hinge are wired to
 * the ARM7's EXTKEYIN (0x04000136 on the ARM7), which the ARM9 cannot read.
 * The ARM7 firmware copies them into the halfword at 0x02FFFFA8 and keeps it
 * current; the ARM9 side reads it with plain loads (the SDK's PAD_Read and
 * PAD_DetectFold inlines, as matched in func_02035F38.c, func_0200FC0C.c,
 * func_0200F83C.c; the lid alone in sub_0203545C.c, func_020358A0.c,
 * func_020774C0.c and DWCi_IsFold).  What they read fixes the layout:
 *
 *   PAD_Read      ((KEYINPUT | xy) ^ 0x2FFF) & 0x2FFF, so X (bit 10), Y
 *                 (bit 11) and DEBUG (bit 13) are active low like KEYINPUT,
 *                 and every other bit below 14 must read 0 for KEYINPUT's
 *                 own bits to survive the OR;
 *   PAD_DetectFold  (xy & 0x8000) >> 15, so bit 15 is set while the lid is
 *                 closed.
 *
 * Nothing in the matched corpus reads the NTR mirror at 0x027FFFA8 (every
 * reader names 0x02FFFFA8), and the port's main RAM is flat TWL RAM where
 * the two are different bytes, so only 0x02FFFFA8 is written.
 *
 * The state comes from sdk/input.h (the buttons and hinge the port's input
 * layer holds).  The DS's ARM7 refreshes the halfword continuously; here it
 * is written at ARM7 power-on and at every frame boundary
 * (bw_input_frame), which is as often as the input can change.
 */

#include "arm7.h"
#include "dsmem.h"
#include "input.h"

static u16 xy_word(void)
{
    BwInputState s;

    bw_input_current(&s);
    return (u16)((~s.buttons & BW_KEY_XY) | (s.lid_closed ? BW_XY_FOLD : 0u));
}

void bw_arm7_pad_frame(void)
{
    *(volatile u16 *)bw_ds_ptr(BW_HW_BUTTON_XY_BUF) = xy_word();
}

void bw_arm7_pad_init(void)
{
    bw_arm7_pad_frame();
}
