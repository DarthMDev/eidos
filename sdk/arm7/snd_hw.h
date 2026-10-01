/*
 * sdk/arm7/snd_hw.h -- the DS sound hardware the ARM7 driver programs: 16
 * channels, two capture units and the master control, as a sample-exact
 * software model.
 *
 * Why a stand-in: sdk/arm7/arm7.h.  The ARM7's sound driver
 * (sdk/arm7/snd_seq.c for sequences, sdk/arm7/snd_driver.c for the direct
 * channel commands) writes these registers and nothing else makes sound, so
 * modelling the hardware once gives both paths the same channel behaviour:
 * the sequencer's notes and a stream's ring buffer go through the same
 * timer, volume, divider and pan arithmetic.
 *
 * Everything here is public NDS hardware documentation (GBATEK, "DS Sound"):
 * SOUNDxCNT (volume 0-127, divider /1 /2 /4 /16, hold, pan 0-127, PSG duty,
 * repeat mode, format, busy), SOUNDxSAD/TMR/PNT/LEN, the channel timer
 * clocked at 33513982 / 2 Hz, PCM8/PCM16/IMA-ADPCM with the ADPCM state saved
 * at the loop start, the PSG square on channels 8-13 (eight steps per cycle,
 * duty n high for n + 1 steps, 7 never high), the noise LFSR on channels 14
 * and 15 (seed 0x7FFF, shift right, a carry sets the output low and XORs
 * 0x6000), SOUNDCNT (master volume, the left/right output source, channel 1
 * and 3 to the mixer or not) and SNDCAPxCNT (capture 0 from the left mixer
 * or channel 0, capture 1 from the right mixer or channel 2, clocked by the
 * timer of channel 1 and 3, PCM16 or PCM8, loop or one-shot).
 *
 * Host shape: the model is stepped in BW_AUDIO_RATE output frames.  A
 * channel advances by its timer in 32.32 fixed point, so no floating point
 * reaches a sample and every host produces the same bits.  Sample data is
 * read in place from wherever the driver resolved it (the game's own memory)
 * and never copied, so a stream refilled behind the read position plays what
 * the ARM9 wrote.  Not modelled: the hardware's few-sample start-up delay and
 * its 10-bit PWM output stage (the host output is 16 bits).
 */

#ifndef BW_PORT_ARM7_SND_HW_H
#define BW_PORT_ARM7_SND_HW_H

#include <stddef.h>

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BW_SNDHW_CHANNELS 16
#define BW_SNDHW_CAPTURES 2

/* The channel timer clock, 33513982 / 2 Hz (GBATEK).                      */
#define BW_SNDHW_CLOCK    16756991u

/* SOUNDxCNT format and repeat fields; the SDK's own enums
 * (SND_WAVE_FORMAT_*, SND_CHANNEL_LOOP_*) carry the same numbers.          */
enum {
    BW_SNDHW_FMT_PCM8  = 0,
    BW_SNDHW_FMT_PCM16 = 1,
    BW_SNDHW_FMT_ADPCM = 2,
    BW_SNDHW_FMT_PSG   = 3      /* square on 8-13, noise on 14-15 */
};
enum {
    BW_SNDHW_REPEAT_MANUAL = 0,
    BW_SNDHW_REPEAT_LOOP   = 1,
    BW_SNDHW_REPEAT_ONESHOT = 2
};

void bw_sndhw_reset(void);

/*
 * Channel registers.  `timer` is the period in timer clocks (the SDK's
 * timer values, 0x10000 - SOUNDxTMR); 0 stops the clock.  `src` is the
 * sample data and `avail` how many bytes may be read from it (0: trust
 * PNT + LEN).  `pnt` and `len` are in words, as the registers are.  Setting
 * up a channel stops it; bw_sndhw_start() starts it.
 */
void bw_sndhw_setup_pcm(int ch, int format, const void *src, size_t avail,
                        int repeat, u32 pnt, u32 len,
                        int volume, int shift, u32 timer, int pan);
void bw_sndhw_setup_psg(int ch, int duty, int volume, int shift, u32 timer, int pan);
void bw_sndhw_setup_noise(int ch, int volume, int shift, u32 timer, int pan);
void bw_sndhw_set_timer(int ch, u32 timer);
void bw_sndhw_set_volume(int ch, int volume, int shift);
void bw_sndhw_set_pan(int ch, int pan);
void bw_sndhw_set_hold(int ch, int hold);
void bw_sndhw_start(int ch);
void bw_sndhw_stop(int ch);
int  bw_sndhw_busy(int ch);
/* The SOUNDxCNT image: volume, divider << 8, hold << 15, pan << 16,
 * duty << 24, repeat << 27, format << 29, busy << 31.                     */
u32  bw_sndhw_cnt(int ch);
/* The period SOUNDxTMR holds, as set (0x10000 - the register).            */
u32  bw_sndhw_timer(int ch);
/* The data a channel reads, for invalidation: [src, src + bytes).         */
const void *bw_sndhw_source(int ch, size_t *bytes);

/* Captures.  `in` 0 = mixer, 1 = channel 0 (capture 0) or 2 (capture 1);
 * `out` 1 adds channel 0 (2) into channel 1's (3's) output, as SNDCAPxCNT
 * bit 0 does; `format` 0 = PCM16, 1 = PCM8 (the SDK's SND_CAPTURE_FORMAT_*);
 * `length` in words.                                                      */
void bw_sndhw_setup_capture(int cap, int format, void *dst, size_t avail, u32 length,
                            int loop, int in, int out);
void bw_sndhw_capture_start(int cap);
void bw_sndhw_capture_stop(int cap);
int  bw_sndhw_capture_busy(int cap);

/* SOUNDCNT: master volume 0-127; output sources (0 mixer, 1 channel 1,
 * 2 channel 3, 3 channels 1 + 3); whether channel 1 / 3 bypass the mixer. */
void bw_sndhw_set_master_volume(int volume);
int  bw_sndhw_master_volume(void);
void bw_sndhw_set_output(int left, int right, int ch1_bypass, int ch3_bypass);
/* SND_SetMasterPan: every channel's pan replaced by `pan` (0-127); -1, as
 * SND_ResetMasterPan sends it, restores each channel's own.               */
void bw_sndhw_set_master_pan(int pan);
int  bw_sndhw_master_pan(void);
/* SND_SetSurroundDecay: 0 off.  Choice (no sender in this game, nothing
 * public on its curve): a channel's level falls linearly with its distance
 * from centre, by decay * |pan - 64| / 8192.                              */
void bw_sndhw_set_surround_decay(int decay);

/* Run the hardware for `frames` frames at BW_AUDIO_RATE, writing that many
 * interleaved stereo frames to `out` when it is non-NULL.                 */
void bw_sndhw_mix(s16 *out, u32 frames);

#ifdef __cplusplus
}
#endif

#endif /* BW_PORT_ARM7_SND_HW_H */
