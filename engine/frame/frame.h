/*
 * engine/frame/frame.h -- the per-frame skeleton the game will run on.
 *
 * This is deliberately free of SDL: it is the DS's frame *convention*, not
 * the host's window.  The platform layer drives it (engine/platform/), a
 * headless test can drive it with no window at all, and nothing in here
 * knows which is which.
 *
 * The DS convention, and why the order of the three callbacks is what it is:
 *
 *   The game's main loop runs during the visible period.  It builds the next
 *   picture into its own shadow copies of OAM and of the BG registers, and
 *   then waits for vblank.  The vblank interrupt is where those shadows are
 *   committed to the hardware, because OAM and the BG registers may not be
 *   written while the display is scanning them out.  The picture the user
 *   sees in the frame after that is the one the commit installed.
 *
 *   So one iteration is: step (the game's own frame of logic), vblank (the
 *   commit, the tick counters, the vblank-time tasks), render (the
 *   compositor turning the committed state into pixels, which on hardware is
 *   what the LCD does on its own).  A port that renders before committing
 *   shows the previous frame's OAM, which is a one-frame lag on everything
 *   that moves.
 *
 * The task-callback list (aletheia's src/matched/sub_0203101C.c and
 * sub_020310C4.c, tcbl.c) is the game's own scheduler over this: one pass of
 * sub_020310C4 per frame walks every registered node and calls its callback.
 * That pass is what a caller will register as the step function; this layer
 * does not reimplement it, because it is matched game code and will be
 * linked, not rewritten.
 */

#ifndef EIDOS_ENGINE_FRAME_H
#define EIDOS_ENGINE_FRAME_H

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* The DS timebase                                                     */
/* ------------------------------------------------------------------ */

/*
 * The system clock and the frame's clock count: 355 dots x 263 lines x 6
 * clocks per dot.  59.8261 frames per second, not 60.
 */
#define EIDOS_FRAME_SYSTEM_CLOCK 33513982u
#define EIDOS_FRAME_CLOCKS         560190u

/*
 * The OS tick.  aletheia's src/matched/func_02021444.c (palanm.c) stores
 * OS_GetTick()'s full u64 into a state block, so the port needs a tick
 * counter with the same unit and the same width.
 *
 * The unit is the system clock divided by 64, i.e. 523655.96875 Hz, which is
 * what the SDK's timer 0 prescaler produces -- *not* 64 Hz.  The "64" in the
 * DS tick is the prescaler's divisor, not a frequency.  One DS frame is
 * 560190 / 64 = 8752 ticks and 62/64 of another, so the counter advances by
 * 8752 per frame and carries the 62 in a remainder accumulator that adds a
 * whole tick every 32 frames.  Dropping that remainder would run the clock
 * slow by half a second an hour.
 */
#define EIDOS_TICKS_PER_SECOND (EIDOS_FRAME_SYSTEM_CLOCK / 64u)

typedef void (*EidosStepFn)(void *user);
typedef void (*EidosVBlankFn)(void *user);
typedef void (*EidosRenderFn)(void *user, u8 *top, u8 *bottom);

/*
 * `render` is handed two caller-owned 256x192x4 RGBA buffers; the loop owns
 * neither and passes through whatever the host gave it.  Any of the three
 * may be NULL.
 */
typedef struct EidosFrameHooks {
    EidosStepFn   step;     /* the game's frame of logic (tcbl.c's pass)  */
    EidosVBlankFn vblank;   /* commit OAM and the BG registers            */
    EidosRenderFn render;   /* compose the committed state into pixels    */
    void         *user;
} EidosFrameHooks;

typedef struct EidosFrameLoop {
    EidosFrameHooks hooks;
    u64 frame;        /* frames completed since the loop was reset       */
    u64 tick;         /* OS_GetTick's unit, EIDOS_TICKS_PER_SECOND Hz    */
    u32 tick_rem;     /* the accumulated 1/64th-clock remainder          */
    u8 *top;
    u8 *bottom;
} EidosFrameLoop;

/* Zero the loop and install the hooks and the two frame buffers. */
void eidos_frame_init(EidosFrameLoop *loop, const EidosFrameHooks *hooks,
                      u8 *top, u8 *bottom);

/*
 * Run exactly one frame: step, then vblank (where the frame and tick
 * counters advance, as they do on hardware where the vblank interrupt is
 * what the OS counts), then render.
 */
void eidos_frame_run_one(EidosFrameLoop *loop);

/* The frame counter, and the tick counter in OS_GetTick's unit. */
u64  eidos_frame_count(const EidosFrameLoop *loop);
u64  eidos_frame_tick(const EidosFrameLoop *loop);

/*
 * The process-wide loop, if one has been made current.  sdk/os_tick.c's
 * OS_GetTick reads this, which is how matched code that asks the OS for the
 * time gets the frame loop's answer rather than the host clock's.
 */
void            eidos_frame_set_current(EidosFrameLoop *loop);
EidosFrameLoop *eidos_frame_current(void);

/* Convert between ticks and milliseconds the way the SDK's macros do. */
u64 eidos_ticks_to_ms(u64 ticks);
u64 eidos_ms_to_ticks(u64 ms);

#ifdef __cplusplus
}
#endif

#endif /* EIDOS_ENGINE_FRAME_H */
