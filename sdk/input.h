/*
 * sdk/input.h -- the player's input as the DS's hardware presents it.
 *
 * The DS delivers input to the ARM9 in three places, and the matched code
 * reads all three directly:
 *
 *   KEYINPUT (0x04000130)  A, B, SELECT, START, the d-pad, R and L, bits
 *                          0-9, active low.  An ARM9 register: the SDK's
 *                          PAD_Read inline loads it (func_02035F38.c, the
 *                          game's per-frame pad update).
 *   0x02FFFFA8             HW_BUTTON_XY_BUF, a halfword the ARM7 keeps
 *                          current, because X, Y, the debug button and the
 *                          hinge are wired to the ARM7's EXTKEYIN only: X
 *                          bit 10, Y bit 11, DEBUG bit 13, active low, and
 *                          bit 15 set while the lid is closed.  PAD_Read
 *                          ORs it into KEYINPUT and masks with 0x2FFF;
 *                          PAD_DetectFold reads bit 15 (sub_0203545C.c,
 *                          func_020358A0.c, func_020774C0.c).
 *   0x02FFFFAA             HW_TOUCHPANEL_BUF, the ARM7's last touch-panel
 *                          sample, read by the ARM9's TP callback
 *                          (TPi_TpCallback.c) when the ARM7 says it is
 *                          there.  sdk/arm7/tp.c owns that protocol.
 *
 * This module is the hardware those three read: the state of the buttons,
 * the hinge and the stylus.  Two sources feed it and are merged every
 * frame: the host (the platform layer's keyboard, gamepads and mouse, set
 * with bw_input_set_host) and a script of timed events (tests, recorded
 * runs, eidos_boot), so a run can be driven with no window and repeated
 * exactly.  bw_input_frame, once per DS frame, applies the script's events
 * for that frame, writes KEYINPUT, and lets the ARM7 stand-in publish the
 * XY halfword and take its automatic touch samples.
 *
 * Nothing here knows SDL.  The bindings from host keys and gamepad buttons
 * to these bits are the platform layer's tables (engine/platform/), which a
 * caller may replace; this layer only ever sees DS buttons.
 */

#ifndef BW_PORT_INPUT_H
#define BW_PORT_INPUT_H

#include <stddef.h>

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The button word: the DS's own bit order (KEYINPUT for bits 0-9, the XY
 * halfword for 10, 11 and 13), so PAD_Read's result and this word name a
 * button with the same bit.  Active high: a set bit is a button held.
 */
#define BW_KEY_A        0x0001u
#define BW_KEY_B        0x0002u
#define BW_KEY_SELECT   0x0004u
#define BW_KEY_START    0x0008u
#define BW_KEY_RIGHT    0x0010u
#define BW_KEY_LEFT     0x0020u
#define BW_KEY_UP       0x0040u
#define BW_KEY_DOWN     0x0080u
#define BW_KEY_R        0x0100u
#define BW_KEY_L        0x0200u
#define BW_KEY_X        0x0400u
#define BW_KEY_Y        0x0800u
#define BW_KEY_DEBUG    0x2000u
#define BW_KEY_KEYINPUT 0x03FFu         /* the bits KEYINPUT carries       */
#define BW_KEY_XY       0x2C00u         /* the bits the ARM7 carries       */
#define BW_KEY_ALL      0x2FFFu         /* PAD_Read's mask                 */

/* Where the ARM9 reads them.                                              */
#define BW_REG_KEYINPUT     0x04000130u
#define BW_HW_BUTTON_XY_BUF 0x02FFFFA8u
#define BW_XY_FOLD          0x8000u     /* bit 15: the lid is closed       */

/* The bottom screen, where the touch panel is.                            */
#define BW_TOUCH_WIDTH  256
#define BW_TOUCH_HEIGHT 192

typedef struct BwInputState {
    u32 buttons;        /* BW_KEY_* held                                    */
    int touch;          /* non-zero: the stylus is on the panel             */
    int touch_x;        /* bottom-screen pixel, 0..255                      */
    int touch_y;        /* bottom-screen pixel, 0..191                      */
    int lid_closed;     /* non-zero: the hinge reads closed                 */
} BwInputState;

/* ------------------------------------------------------------------ */
/* Life cycle and the frame                                            */
/* ------------------------------------------------------------------ */

/*
 * Everything released, the lid open, the script emptied, the frame counter
 * at 0.  The input state is not part of the DS address space, so
 * bw_port_reset leaves it alone; a caller starting a new run calls this.
 */
void bw_input_reset(void);

/*
 * The DS frame boundary.  In order: every script event due at or before the
 * current frame number is applied, in the order it was added; the merged
 * state (below) becomes what the hardware reads; KEYINPUT is written; the
 * ARM7 publishes the XY halfword and, if the ARM9 started automatic
 * touch-panel sampling, takes this frame's samples (sdk/arm7/arm7.h); then
 * the frame number advances.  The frame driver calls it once per frame,
 * before the vblank interrupt is delivered, which is where the DS's ARM7
 * does the same.  Needs the address space (bw_ds_reset or bw_port_reset).
 */
void bw_input_frame(void);

/* The number of the frame the next bw_input_frame applies (0 first).      */
u32 bw_input_frame_number(void);

/*
 * The state the hardware reads now: the host's buttons ORed with the
 * script's, the script's stylus if it has one down and the host's
 * otherwise, the lid closed if either says so.  Coordinates are clamped to
 * the screen.  Updated by bw_input_frame, and at once by bw_input_publish.
 */
void bw_input_current(BwInputState *out);

/*
 * Make the merged state current and write it to the hardware now, without
 * applying script events or advancing the frame.  For a host that wants a
 * change seen within the frame, and for tests.
 */
void bw_input_publish(void);

/* ------------------------------------------------------------------ */
/* The host                                                            */
/* ------------------------------------------------------------------ */

/*
 * The host's input, replacing the previous call's.  The platform layer
 * calls this after every pump; it takes effect at the next bw_input_frame
 * (or bw_input_publish).
 */
void bw_input_set_host(const BwInputState *s);

/* ------------------------------------------------------------------ */
/* The script                                                          */
/* ------------------------------------------------------------------ */

/*
 * Timed events, each applied at the start of frame `frame` (the
 * bw_input_frame call that sees that frame number).  Script buttons and the
 * script stylus are held until released; `hold` > 0 schedules the release
 * that many frames later, so press(10, A, 1) holds A for exactly frame 10.
 * Events for a frame already past are applied at the next frame.  Every
 * call returns 0, or -1 if out of memory or the arguments are invalid
 * (no button bit, a touch off the screen).
 */
int bw_input_script_press(u32 frame, u32 buttons, u32 hold);
int bw_input_script_release(u32 frame, u32 buttons);
int bw_input_script_touch(u32 frame, int x, int y, u32 hold);
int bw_input_script_untouch(u32 frame);
int bw_input_script_lid(u32 frame, int closed);

/* Drop every event not yet applied and release what the script holds.    */
void bw_input_script_clear(void);

/* Events not yet applied.                                                 */
u32 bw_input_script_pending(void);

/*
 * Add the events of a text script, one per line; `#` starts a comment.
 *
 *     <frame> press <buttons> [<hold>]
 *     <frame> release <buttons>
 *     <frame> touch <x> <y> [<hold>]
 *     <frame> untouch
 *     <frame> lid open|closed
 *
 * <buttons> is one or more button names joined by '+' (A+B, UP+R), names
 * as bw_input_button_name gives them, any case.  Frames are decimal and
 * absolute.  Returns 0, or -1 with "line N: why" in `err` (may be NULL)
 * and no event of the text added.
 */
int bw_input_script_load(const char *text, char *err, size_t err_size);

/* ------------------------------------------------------------------ */
/* Names                                                               */
/* ------------------------------------------------------------------ */

/*
 * The button with this name ("A", "B", "SELECT", "START", "RIGHT", "LEFT",
 * "UP", "DOWN", "R", "L", "X", "Y", "DEBUG"; any case), or 0.  For binding
 * tables and scripts written as text.
 */
u32 bw_input_button_by_name(const char *name);

/* The name of a single button bit, or NULL.                               */
const char *bw_input_button_name(u32 button);

#ifdef __cplusplus
}
#endif

#endif /* BW_PORT_INPUT_H */
