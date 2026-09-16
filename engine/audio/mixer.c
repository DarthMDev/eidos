/*
 * engine/audio/mixer.c -- one voice: resample, envelope, pan, and the two
 * PSG generators.
 *
 * A voice is the whole of the per-note arithmetic, and it is chunked: the
 * state it carries (the source position, the square's phase, the noise
 * LFSR, the sample counter) is exactly what makes rendering a note in
 * pieces bit-identical to rendering it in one call, which is what lets the
 * offline oracle comparison and the real-time callback share this code.
 *
 * Every rule here is tools/assets/sseqsynth.py's, and the places where that
 * tool says it is approximating are approximated the same way rather than
 * improved, because the point of the comparison is that two independent
 * implementations of the *same* model agree:
 *
 *   - **pitch**: the source advances at 2^((played - base) / 12) *
 *     native_rate / output_rate source samples per output sample, with
 *     linear interpolation between the two neighbouring samples.
 *   - **looping**: past the loop point the source index wraps into
 *     [loop_start, total_len), and the interpolation partner of the last
 *     sample is the loop point rather than the end.  A one-shot wave is
 *     played out to at least its own natural length whatever the note's
 *     gate says, which is what makes a cry (gate duration 0) sound.
 *   - **envelope**: a linear ADSR over the instrument's four u8 fields,
 *     byte value used directly as a fraction of the stage's maximum time
 *     (attack 0.15 s, decay 0.3 s, release 0.4 s with a 0.02 s floor), so
 *     a low byte is a fast stage.  That direction is the asset lane's
 *     empirical choice, not a spec; it is reproduced, not re-decided.
 *   - **pan** is a linear crossfade about 64, not equal power, and volume,
 *     expression, velocity and the SEQ record's own volume multiply
 *     linearly rather than in dB.  Both are documented approximations of
 *     the hardware's real curves.
 *   - **PSG square** is a bipolar rectangle whose high fraction is
 *     (duty + 1) / 8, the DS PSG channel's eight duty settings.
 *   - **PSG noise** is the 15-bit Galois LFSR clocked at the *channel
 *     timer* rate the SDK's SND timer formula derives from the note's
 *     frequency (timer = -(clock / frequency), clock = 33513982/2 Hz), then
 *     sample-and-held to the output rate by nearest neighbour, because an
 *     LFSR bit is a held digital level between clocks.
 *
 * One of those deserves to be called out, because reproducing it looks like
 * a bug and is not: **the noise LFSR's all-ones reset value is a fixed
 * point of its own update.** Right-shifting an all-ones value always shifts
 * out a 1, so the tap is always XORed back in and the value is all ones
 * again, forever; every noise note is therefore a constant +1 level shaped
 * by the envelope rather than noise.  aletheia's sseqsynth.py docstring
 * found and documented this and deliberately left it (its own A/B proof:
 * the clock-rate fix changed nothing, byte for byte).  The engine
 * reproduces it exactly so the comparison is a comparison; whoever fixes
 * the seed fixes it in the tool first, and this test then measures the
 * change instead of hiding it.
 */

#include "audio.h"

#include <math.h>
#include <string.h>

/* CPython's float.__round__ with no ndigits: C round() (half away from
 * zero), and where the value sits exactly on a half, 2 * round(x / 2),
 * which is round-half-to-even.  Python's round() is used by the oracle for
 * the note start sample, the note length, the timer period and the final
 * quantisation, so getting this wrong shifts whole notes by one sample. */
double bw_audio_py_round(double x)
{
    double r = round(x);
    if (fabs(x - r) == 0.5) r = 2.0 * round(x / 2.0);
    return r;
}

double bw_audio_note_freq_hz(int played_note)
{
    return 440.0 * pow(2.0, (played_note - 69) / 12.0);
}

long bw_audio_note_timer_ticks(double freq_hz, double *actual_hz)
{
    double clock = BW_AUDIO_TIMER_CLOCK_HZ;
    long ticks;
    if (freq_hz <= 0.0) {
        if (actual_hz != NULL) *actual_hz = 0.0;
        return 0;
    }
    ticks = (long)bw_audio_py_round(clock / freq_hz);
    if (ticks < 1) ticks = 1;
    if (actual_hz != NULL) *actual_hz = clock / (double)ticks;
    return ticks;
}

/* ------------------------------------------------------------------ */
/* Envelope                                                            */
/* ------------------------------------------------------------------ */

static void adsr_times(const BwRegion *r, double *attack, double *decay,
                       double *sustain_level, double *release)
{
    *attack = r->attack / 127.0 * BW_AUDIO_ATTACK_MAX;
    *decay  = r->decay  / 127.0 * BW_AUDIO_DECAY_MAX;
    *release = r->release / 127.0 * BW_AUDIO_RELEASE_MAX;
    if (*release < BW_AUDIO_MIN_RELEASE) *release = BW_AUDIO_MIN_RELEASE;
    *sustain_level = r->sustain / 127.0;
}

static double envelope_pre_release(double t, double attack_s, double decay_s,
                                   double sustain_level)
{
    double t2;
    if (attack_s > 0.0 && t < attack_s) return t / attack_s;
    t2 = t - attack_s;
    if (decay_s > 0.0 && t2 < decay_s)
        return 1.0 + (sustain_level - 1.0) * (t2 / decay_s);
    return sustain_level;
}

static double envelope_value(double t, double attack_s, double decay_s,
                             double sustain_level, double note_off_t, double release_s)
{
    double level_at_off, frac;
    if (t < note_off_t)
        return envelope_pre_release(t, attack_s, decay_s, sustain_level);
    level_at_off = envelope_pre_release(note_off_t, attack_s, decay_s, sustain_level);
    frac = (t - note_off_t) / release_s;
    if (frac >= 1.0) return 0.0;
    return level_at_off * (1.0 - frac);
}

/* ------------------------------------------------------------------ */
/* Starting a voice                                                    */
/* ------------------------------------------------------------------ */

static void set_pan_gains(BwAudioVoice *v, int pan, double gain)
{
    double pan_norm = (pan - 64) / 64.0;
    double l, r;
    if (pan_norm < -1.0) pan_norm = -1.0;
    if (pan_norm > 1.0) pan_norm = 1.0;
    l = 1.0 - pan_norm; if (l > 1.0) l = 1.0;
    r = 1.0 + pan_norm; if (r > 1.0) r = 1.0;
    v->left_gain = gain * l;
    v->right_gain = gain * r;
}

int bw_audio_voice_start_wave(BwAudioVoice *v, const s16 *samples,
                              size_t total_len, size_t loop_start, int loop_flag,
                              double native_rate, const BwRegion *region,
                              int played_note, int pan, double gain,
                              double start_time, double note_off_time,
                              double sample_rate)
{
    double pitch_ratio, step, note_off_rel, total_rel;

    memset(v, 0, sizeof(*v));
    if (total_len == 0) return -1;

    pitch_ratio = pow(2.0, (played_note - (int)region->base_note) / 12.0);
    if (native_rate == 0.0) native_rate = 16000.0;
    step = pitch_ratio * native_rate / sample_rate;
    if (step <= 0.0) return -1;

    v->loop_flag = loop_flag && loop_start < total_len - 1;
    adsr_times(region, &v->attack_s, &v->decay_s, &v->sustain_level, &v->release_s);

    note_off_rel = note_off_time - start_time;
    if (!v->loop_flag) {
        double natural = total_len / step / sample_rate;
        if (natural > note_off_rel) note_off_rel = natural;
    }
    if (note_off_rel < 0.0) note_off_rel = 0.0;
    total_rel = note_off_rel + v->release_s;
    if (total_rel > BW_AUDIO_MAX_NOTE_SECONDS) total_rel = BW_AUDIO_MAX_NOTE_SECONDS;

    v->note_off_rel = note_off_rel;
    v->n_out = (size_t)bw_audio_py_round(total_rel * sample_rate);
    if (v->n_out < 1) v->n_out = 1;

    set_pan_gains(v, pan, gain);
    v->start_sample = (long)bw_audio_py_round(start_time * sample_rate);
    v->samples = samples;
    v->total_len = total_len;
    v->loop_start = loop_start;
    v->span = total_len - loop_start;
    v->step = step;
    v->src_pos = 0.0;
    v->psg = 0;
    v->i = 0;
    v->sample_rate = sample_rate;
    v->active = 1;
    return 0;
}

int bw_audio_voice_start_psg(BwAudioVoice *v, const BwRegion *region,
                             int played_note, int pan, double gain,
                             double start_time, double note_off_time,
                             double sample_rate)
{
    double freq, note_off_rel, total_rel;

    memset(v, 0, sizeof(*v));
    if (region->kind != BW_REGION_PSG_SQUARE && region->kind != BW_REGION_PSG_NOISE)
        return -1;

    freq = bw_audio_note_freq_hz(played_note);
    adsr_times(region, &v->attack_s, &v->decay_s, &v->sustain_level, &v->release_s);

    note_off_rel = note_off_time - start_time;
    if (note_off_rel < 0.0) note_off_rel = 0.0;
    /* A PSG tone has no natural decay length, so a very short gate still
     * gets a floor of 50 ms before the release runs. */
    if (note_off_rel < 0.05) note_off_rel = 0.05;
    total_rel = note_off_rel + v->release_s;
    if (total_rel > BW_AUDIO_MAX_NOTE_SECONDS) total_rel = BW_AUDIO_MAX_NOTE_SECONDS;

    v->note_off_rel = note_off_rel;
    v->n_out = (size_t)bw_audio_py_round(total_rel * sample_rate);
    if (v->n_out < 1) v->n_out = 1;

    /* The PSG output is +-1, so it is scaled to full scale and sat a
     * quarter down against the PCM path's own 16-bit sample units. */
    set_pan_gains(v, pan, gain * 0.25 * 32767.0);
    v->start_sample = (long)bw_audio_py_round(start_time * sample_rate);
    v->i = 0;

    if (region->kind == BW_REGION_PSG_SQUARE) {
        v->psg = 1;
        v->duty = (int)region->duty;
        v->psg_step = (freq > 0.0) ? freq / sample_rate : 0.0;
        v->phase = 0.0;
        v->psg_freq_valid = (freq > 0.0);
    } else {
        double actual = 0.0;
        v->psg = 2;
        v->psg_freq_valid = (freq > 0.0);
        if (v->psg_freq_valid) {
            bw_audio_note_timer_ticks(freq, &actual);
            if (actual <= 0.0) v->psg_freq_valid = 0;
        }
        if (v->psg_freq_valid) {
            double native;
            v->psg_step = actual / sample_rate;
            native = ceil((double)v->n_out * v->psg_step) + 2.0;
            v->n_native = (size_t)((native < 1.0) ? 1.0 : native);
        } else {
            v->psg_step = 0.0;
            v->n_native = 1;
        }
        v->lfsr = 0x7FFFu;          /* the hardware reset value           */
        v->native_i = 0;
        v->native_last = 0.0;
        v->src_pos = 0.0;
    }
    v->sample_rate = sample_rate;
    v->active = 1;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Source generation                                                   */
/* ------------------------------------------------------------------ */

static double noise_at(BwAudioVoice *v, size_t idx)
{
    if (idx >= v->n_native) idx = v->n_native - 1;
    while (v->native_i <= idx) {
        u32 bit0 = v->lfsr & 1u;
        v->lfsr >>= 1;
        /* Shift right, and on a carry XOR bits 14 and 13 back in. */
        if (bit0) v->lfsr ^= 0x6000u;
        v->native_last = bit0 ? 1.0 : -1.0;
        v->native_i++;
    }
    return v->native_last;
}

/* The source sample for the voice's current index, advancing whatever
 * state the generator carries.  Called once per output sample whether or
 * not that sample lands inside the caller's block, so that chunking cannot
 * change the result. */
static double voice_source(BwAudioVoice *v)
{
    if (v->psg == 1) {
        double duty_frac = (v->duty + 1) / 8.0;
        double out;
        if (!v->psg_freq_valid) return 0.0;
        out = (fmod(v->phase, 1.0) < duty_frac) ? 1.0 : -1.0;
        v->phase += v->psg_step;
        return out;
    }
    if (v->psg == 2) {
        double out;
        if (!v->psg_freq_valid) return 0.0;
        out = noise_at(v, (size_t)(v->src_pos + 0.5));
        v->src_pos += v->psg_step;
        return out;
    }
    {
        size_t i0 = (size_t)v->src_pos;
        size_t i1;
        double frac, s0, s1;
        if (v->loop_flag && i0 >= v->loop_start) {
            if (v->span > 0) i0 = v->loop_start + (i0 - v->loop_start) % v->span;
            else i0 = v->total_len - 1;
            i1 = i0 + 1;
            if (i1 >= v->total_len) i1 = v->loop_start;
        } else if (i0 >= v->total_len - 1) {
            i0 = v->total_len - 1;
            i1 = i0;
        } else {
            i1 = i0 + 1;
        }
        frac = v->src_pos - (double)(size_t)v->src_pos;
        s0 = (double)v->samples[i0];
        s1 = (double)v->samples[i1];
        v->src_pos += v->step;
        return s0 + (s1 - s0) * frac;
    }
}

void bw_audio_voice_mix(BwAudioVoice *v, double *left, double *right,
                        size_t n, long block_start)
{
    long block_end = block_start + (long)n;

    if (!v->active) return;
    while (v->i < v->n_out) {
        long out_idx = v->start_sample + (long)v->i;
        double s, env, val;
        if (out_idx >= block_end) return;
        s = voice_source(v);
        if (out_idx >= block_start) {
            env = envelope_value((double)v->i / v->sample_rate, v->attack_s, v->decay_s,
                                 v->sustain_level, v->note_off_rel, v->release_s);
            val = s * env;
            left[out_idx - block_start] += val * v->left_gain;
            right[out_idx - block_start] += val * v->right_gain;
        }
        v->i++;
    }
    v->active = 0;
}
