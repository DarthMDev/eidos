/*
 * engine/frame/prof.h -- a cheap per-phase frame profiler.
 *
 * Off by default (one predictable branch per probe).  With it on, every
 * `every` frames it prints the average and maximum milliseconds each phase
 * took in that window.  Single-threaded: the frame thread owns it.
 */
#ifndef EIDOS_FRAME_PROF_H
#define EIDOS_FRAME_PROF_H

#ifdef __cplusplus
extern "C" {
#endif

enum {
    EIDOS_PROF_STEP,      /* the game: input, step, vblank              */
    EIDOS_PROF_G3D,       /* the 3D frame                               */
    EIDOS_PROF_MAIN,      /* the main engine's 2D compose               */
    EIDOS_PROF_SUB,       /* the sub engine's 2D compose                */
    EIDOS_PROF_PRESENT,   /* upload and present                         */
    EIDOS_PROF_AUDIO,     /* sound render and queue                     */
    EIDOS_PROF_WAIT,      /* the pacing sleep                           */
    EIDOS_PROF_COUNT
};

extern int eidos_prof_on;

void eidos_prof_enable(unsigned every);
unsigned long long eidos_prof_begin(void);
void eidos_prof_end(int phase, unsigned long long t0);
/* End of a frame: counts it and prints a line when the window is full.   */
void eidos_prof_frame(unsigned long long frame_no);

#ifdef __cplusplus
}
#endif

#endif
