/*
 * tests/test_audio.c -- engine/audio against aletheia's sseqsynth.py.
 *
 * Usage: test_audio <data root> <oracle dir>
 *
 * The oracle directory is written by tools/audio_oracle.py at build time
 * (see CMakeLists.txt): one .wav and one .notes.txt per sequence, plus
 * seqs.txt naming them.  This test renders the same sequences through the C
 * engine and checks two things, in this order, because they fail for
 * different reasons:
 *
 *   1. **the note event list.**  The engine writes its own .engine.notes.txt
 *      in the same format and the two are diffed line for line.  A
 *      difference here is a sequencer difference: a mis-decoded opcode, a
 *      jump taken a different number of times, a tempo segment off by a
 *      tick, a region resolved to the wrong instrument.  The first
 *      differing line is printed in full from both sides.  Every float in
 *      the dump is written as its exact IEEE-754 bit pattern, so the diff
 *      means "the same double" and not "the same 17 digits".
 *   2. **the sample buffers.**  The engine's render is compared against the
 *      oracle's .wav sample for sample, and the peak, the RMS and the
 *      largest difference are reported for each sequence.
 *
 * Tolerance: BW_AUDIO_SAMPLE_TOLERANCE, in int16 LSBs, is what the test
 * enforces.  It is 0 -- the two renders are required to be *identical*, not
 * merely close.  That is reachable because engine/audio computes the same
 * quantities in the same order in binary64 as the oracle does (audio.h's
 * header comment says why, and what a later fixed-point pass would have to
 * relax).  The measured difference is printed either way, so a change that
 * costs exactness says so in numbers rather than by silently passing.
 *
 * The three sequences are picked for coverage of the three channel kinds:
 *
 *   SEQ 1062  SEQ_BGM_GYM          PCM waves out of a real SWAR
 *   SEQ 1146  SEQ_BGM_BATTLEPINCH  PSG square (and PCM alongside it)
 *   SEQ 1379  SEQ_SE_KOUKA_H       PSG noise
 */

#include "port_test.h"
#include "port.h"
#include "audio.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define BW_AUDIO_SAMPLE_TOLERANCE 0

/* ------------------------------------------------------------------ */
/* The note dump: the same line format tools/audio_oracle.py writes    */
/* ------------------------------------------------------------------ */

static void bits64(double x, char *buf)
{
    u64 u;
    memcpy(&u, &x, sizeof(u));
    sprintf(buf, "%016llx", (unsigned long long)u);
}

static const char *kind_name(const BwAudioNoteInfo *inf)
{
    if (!inf->resolved) return "none";
    switch (inf->region.kind) {
    case BW_REGION_PSG_SQUARE: return "square";
    case BW_REGION_PSG_NOISE:  return "noise";
    default:                   return "wave";
    }
}

static int write_engine_notes(const char *path, u32 seq_index,
                              const BwAudioRender *r)
{
    FILE *fh = fopen(path, "wb");   /* binary: LF endings, like the oracle */
    size_t i;
    char b1[32], b2[32], b3[32];

    if (fh == NULL) return -1;
    fprintf(fh, "seq=%u bank=%u tracks=%d events=%d bpm=%d\n",
            seq_index, r->bank_index, r->track_count, r->note_events_total,
            r->master_bpm);
    for (i = 0; i < r->note_count; i++) {
        const BwAudioNoteInfo *inf = &r->notes[i];
        int base = 0, a = 0, d = 0, s = 0, rel = 0, rpan = 0, duty = 0;
        int warc = -1, wid = -1;
        double gain = 0.0;
        if (inf->resolved) {
            base = inf->region.base_note;
            a = inf->region.attack;
            d = inf->region.decay;
            s = inf->region.sustain;
            rel = inf->region.release;
            rpan = inf->region.pan;
            gain = inf->gain;
            if (inf->region.kind == BW_REGION_PSG_SQUARE) duty = (int)inf->region.duty;
            if (inf->region.kind == BW_REGION_WAVE) {
                warc = (int)inf->region.wave_arc;
                wid = (int)inf->region.wave_id;
            }
        }
        bits64(inf->start_time, b1);
        bits64(inf->off_time, b2);
        bits64(gain, b3);
        fprintf(fh,
                "%u t%d tick=%u dur=%u note=%d vel=%d prog=%d vol=%d exp=%d "
                "pan=%d start=%s off=%s region=%s base=%d a=%d d=%d s=%d r=%d "
                "rpan=%d duty=%d warc=%d wid=%d gain=%s\n",
                (unsigned)i, inf->ev.track, inf->ev.tick, inf->ev.dur, inf->ev.note,
                inf->ev.velocity, inf->ev.program, inf->ev.volume, inf->ev.expression,
                inf->ev.pan, b1, b2, kind_name(inf), base, a, d, s, rel,
                rpan, duty, warc, wid, b3);
    }
    bits64(r->duration_seconds, b1);
    fprintf(fh, "duration=%s samples=%u rendered=%d dropped=%d\n",
            b1, (unsigned)r->n_samples, r->notes_rendered, r->notes_dropped);
    fclose(fh);
    return 0;
}

/* Returns 0 when the two files are identical, else the 1-based number of
 * the first differing line, with both lines printed. */
static long diff_files(const char *want_path, const char *got_path)
{
    FILE *a = fopen(want_path, "rb");
    FILE *b = fopen(got_path, "rb");
    char la[1024], lb[1024];
    long line = 0;

    if (a == NULL || b == NULL) {
        printf("  cannot open %s\n", a == NULL ? want_path : got_path);
        if (a) fclose(a);
        if (b) fclose(b);
        return -1;
    }
    for (;;) {
        char *ra = fgets(la, sizeof(la), a);
        char *rb = fgets(lb, sizeof(lb), b);
        line++;
        if (ra == NULL && rb == NULL) { fclose(a); fclose(b); return 0; }
        if (ra == NULL || rb == NULL) {
            printf("  line %ld: %s ended first\n", line, ra == NULL ? "oracle" : "engine");
            fclose(a); fclose(b);
            return line;
        }
        if (strcmp(la, lb) != 0) {
            printf("  line %ld differs\n    oracle: %s    engine: %s", line, la, lb);
            fclose(a); fclose(b);
            return line;
        }
    }
}

/* ------------------------------------------------------------------ */
/* A minimal RIFF/WAVE reader: 16-bit PCM only, which is all the oracle
 * ever writes.  No third-party code and no image or audio library on
 * either side of the comparison.                                      */
/* ------------------------------------------------------------------ */

static u32 rd32le(const u8 *p)
{
    return (u32)p[0] | ((u32)p[1] << 8) | ((u32)p[2] << 16) | ((u32)p[3] << 24);
}

static int read_wav(const char *path, s16 **pcm, size_t *frames, int *channels)
{
    FILE *fh = fopen(path, "rb");
    long size;
    u8 *buf;
    size_t pos;

    *pcm = NULL;
    *frames = 0;
    *channels = 0;
    if (fh == NULL) return -1;
    if (fseek(fh, 0, SEEK_END) != 0 || (size = ftell(fh)) < 12) { fclose(fh); return -1; }
    rewind(fh);
    buf = (u8 *)malloc((size_t)size);
    if (buf == NULL || fread(buf, 1, (size_t)size, fh) != (size_t)size) {
        free(buf); fclose(fh); return -1;
    }
    fclose(fh);
    if (memcmp(buf, "RIFF", 4) != 0 || memcmp(buf + 8, "WAVE", 4) != 0) {
        free(buf); return -1;
    }
    pos = 12;
    while (pos + 8 <= (size_t)size) {
        u32 csize = rd32le(buf + pos + 4);
        const u8 *body = buf + pos + 8;
        if (pos + 8 + csize > (size_t)size) csize = (u32)((size_t)size - pos - 8);
        if (memcmp(buf + pos, "fmt ", 4) == 0 && csize >= 16) {
            *channels = (int)(buf[pos + 8 + 2] | (buf[pos + 8 + 3] << 8));
        } else if (memcmp(buf + pos, "data", 4) == 0) {
            size_t n = csize / 2;
            size_t i;
            s16 *out = (s16 *)malloc((n ? n : 1) * sizeof(s16));
            if (out == NULL) { free(buf); return -1; }
            for (i = 0; i < n; i++)
                out[i] = (s16)(body[i * 2] | (body[i * 2 + 1] << 8));
            *pcm = out;
            *frames = (*channels > 0) ? n / (size_t)*channels : n / 2;
            free(buf);
            return 0;
        }
        pos += 8 + csize + (csize & 1);
    }
    free(buf);
    return -1;
}

/* ------------------------------------------------------------------ */
/* Synthetic checks: no game data, every expected value on paper       */
/* ------------------------------------------------------------------ */

static int check_py_round(void)
{
    /* Python's round() is half-to-even, and the oracle uses it for the note
     * start sample, the note length, the timer period and the final
     * quantisation.  C's round() is half-away-from-zero, so these six are
     * exactly the cases where using it directly would shift a note. */
    BW_CHECK_EQ("py_round 0.5 -> 0", (long)bw_audio_py_round(0.5), 0);
    BW_CHECK_EQ("py_round 1.5 -> 2", (long)bw_audio_py_round(1.5), 2);
    BW_CHECK_EQ("py_round 2.5 -> 2", (long)bw_audio_py_round(2.5), 2);
    BW_CHECK_EQ("py_round -0.5 -> 0", (long)bw_audio_py_round(-0.5), 0);
    BW_CHECK_EQ("py_round -1.5 -> -2", (long)bw_audio_py_round(-1.5), -2);
    BW_CHECK_EQ("py_round 2.4999 -> 2", (long)bw_audio_py_round(2.4999), 2);
    BW_CHECK_EQ("py_round 2.5001 -> 3", (long)bw_audio_py_round(2.5001), 3);
    return 0;
}

static int check_timer(void)
{
    /* The SND timer formula at A4: 16756991 / 440 = 38084.07..., which
     * rounds to 38084 clocks and therefore sounds at 440.0008 Hz rather
     * than at 440.  Every DS channel's pitch is quantised this way. */
    double actual = 0.0;
    long ticks = bw_audio_note_timer_ticks(440.0, &actual);
    BW_CHECK_EQ("timer ticks at 440 Hz", ticks, 38084);
    BW_REQUIRE("timer frequency is within a hundredth of 440 Hz",
               fabs(actual - 440.0) < 0.01);
    BW_CHECK_EQ("note 69 is A4", (long)(bw_audio_note_freq_hz(69) + 0.5), 440);
    BW_CHECK_EQ("note 81 is an octave up", (long)(bw_audio_note_freq_hz(81) + 0.5), 880);
    return 0;
}

/* Rendering a voice in pieces has to be bit-identical to rendering it in
 * one call, or the streaming callback and the offline render are two
 * different synthesizers.  This is the check that says they are one. */
static int check_chunking(void)
{
    static s16 wave[64];
    BwRegion region;
    BwAudioVoice a, b;
    double la[8192], ra[8192], lb[8192], rb[8192];
    size_t i;
    int same = 1;

    for (i = 0; i < 64; i++) wave[i] = (s16)((i % 16) * 1000 - 8000);
    memset(&region, 0, sizeof(region));
    region.kind = BW_REGION_WAVE;
    region.base_note = 60;
    region.attack = 3;
    region.decay = 7;
    region.sustain = 90;
    region.release = 40;
    region.pan = 40;

    memset(la, 0, sizeof(la));
    memset(ra, 0, sizeof(ra));
    memset(lb, 0, sizeof(lb));
    memset(rb, 0, sizeof(rb));

    BW_CHECK_EQ("chunk fixture: voice a starts",
                bw_audio_voice_start_wave(&a, wave, 64, 8, 1, 8000.0, &region,
                                          67, 96, 0.5, 0.0, 0.2, BW_AUDIO_RATE), 0);
    BW_CHECK_EQ("chunk fixture: voice b starts",
                bw_audio_voice_start_wave(&b, wave, 64, 8, 1, 8000.0, &region,
                                          67, 96, 0.5, 0.0, 0.2, BW_AUDIO_RATE), 0);
    bw_audio_voice_mix(&a, la, ra, 8192, 0);
    for (i = 0; i < 8192; i += 37) {
        size_t n = (i + 37 <= 8192) ? 37 : 8192 - i;
        bw_audio_voice_mix(&b, lb + i, rb + i, n, (long)i);
    }
    for (i = 0; i < 8192; i++)
        if (la[i] != lb[i] || ra[i] != rb[i]) { same = 0; break; }
    BW_REQUIRE("a voice rendered in 37-sample chunks is bit-identical", same);

    /* The same for a PSG square, whose phase accumulator is the other piece
     * of state a chunked render could lose. */
    memset(la, 0, sizeof(la));
    memset(lb, 0, sizeof(lb));
    memset(ra, 0, sizeof(ra));
    memset(rb, 0, sizeof(rb));
    region.kind = BW_REGION_PSG_SQUARE;
    region.duty = 3;
    bw_audio_voice_start_psg(&a, &region, 72, 64, 0.5, 0.0, 0.2, BW_AUDIO_RATE);
    bw_audio_voice_start_psg(&b, &region, 72, 64, 0.5, 0.0, 0.2, BW_AUDIO_RATE);
    bw_audio_voice_mix(&a, la, ra, 8192, 0);
    for (i = 0; i < 8192; i += 101) {
        size_t n = (i + 101 <= 8192) ? 101 : 8192 - i;
        bw_audio_voice_mix(&b, lb + i, rb + i, n, (long)i);
    }
    same = 1;
    for (i = 0; i < 8192; i++)
        if (la[i] != lb[i] || ra[i] != rb[i]) { same = 0; break; }
    BW_REQUIRE("a PSG square rendered in 101-sample chunks is bit-identical", same);
    return 0;
}

/* ------------------------------------------------------------------ */
/* One sequence                                                        */
/* ------------------------------------------------------------------ */

static int compare_seq(const BwSdat *sd, u32 seq_index, const char *name,
                       double max_seconds, const char *oracle_dir)
{
    BwAudioRender r;
    char notes_want[1024], notes_got[1024], wav_path[1024];
    s16 *want = NULL;
    size_t want_frames = 0;
    int want_channels = 0;
    size_t i, n_cmp;
    long first_bad = -1;
    long max_diff = 0, n_diff = 0;
    double sum_e = 0.0, sum_o = 0.0;
    long peak_e = 0, peak_o = 0;

    printf("SEQ %u (%s)\n", seq_index, name);
    if (bw_audio_render_seq(sd, seq_index, -1, max_seconds, &r) != 0) {
        printf("  FAIL render failed\n");
        return 1;
    }

    sprintf(notes_want, "%s/seq%04u.notes.txt", oracle_dir, seq_index);
    sprintf(notes_got, "%s/seq%04u.engine.notes.txt", oracle_dir, seq_index);
    sprintf(wav_path, "%s/seq%04u.wav", oracle_dir, seq_index);

    if (write_engine_notes(notes_got, seq_index, &r) != 0) {
        printf("  FAIL cannot write %s\n", notes_got);
        bw_audio_render_free(&r);
        return 1;
    }

    bw_checks++;
    if (diff_files(notes_want, notes_got) != 0) {
        bw_failures++;
        printf("  FAIL note event list differs from the oracle's\n");
        bw_audio_render_free(&r);
        return 1;
    }
    printf("  note events: %d, identical to the oracle (%d rendered, %d dropped, "
           "%d tracks, %d BPM, bank %u, %d voices at once)\n",
           r.note_events_total, r.notes_rendered, r.notes_dropped, r.track_count,
           r.master_bpm, r.bank_index, r.max_concurrent);

    if (read_wav(wav_path, &want, &want_frames, &want_channels) != 0) {
        bw_checks++;
        bw_failures++;
        printf("  FAIL cannot read the oracle wav %s\n", wav_path);
        bw_audio_render_free(&r);
        return 1;
    }

    BW_CHECK_EQ("oracle wav is stereo", want_channels, 2);
    BW_CHECK_EQ("frame counts agree", (long)r.n_samples, (long)want_frames);

    n_cmp = r.n_samples < want_frames ? r.n_samples : want_frames;
    for (i = 0; i < n_cmp * 2; i++) {
        long e = r.pcm[i], o = want[i];
        long d = e - o;
        if (d < 0) d = -d;
        if (d > max_diff) max_diff = d;
        if (d != 0) { n_diff++; if (first_bad < 0) first_bad = (long)i; }
        sum_e += (double)e * (double)e;
        sum_o += (double)o * (double)o;
        if (labs(e) > peak_e) peak_e = labs(e);
        if (labs(o) > peak_o) peak_o = labs(o);
    }
    printf("  samples: %u frames, peak engine %ld / oracle %ld, "
           "RMS engine %.3f / oracle %.3f\n",
           (unsigned)n_cmp, peak_e, peak_o,
           sqrt(sum_e / (double)(n_cmp * 2)), sqrt(sum_o / (double)(n_cmp * 2)));
    printf("  difference: %ld of %u samples differ, largest %ld LSB "
           "(tolerance %d)\n",
           n_diff, (unsigned)(n_cmp * 2), max_diff, BW_AUDIO_SAMPLE_TOLERANCE);
    if (first_bad >= 0) {
        printf("  first difference at sample %ld: engine %d, oracle %d\n",
               first_bad, (int)r.pcm[first_bad], (int)want[first_bad]);
    }

    bw_checks++;
    if (max_diff > BW_AUDIO_SAMPLE_TOLERANCE) {
        bw_failures++;
        printf("  FAIL sample difference %ld exceeds the tolerance %d\n",
               max_diff, BW_AUDIO_SAMPLE_TOLERANCE);
    }
    BW_CHECK_EQ("the render is not silent", peak_e > 100, 1);

    free(want);
    bw_audio_render_free(&r);
    return 0;
}

/* ------------------------------------------------------------------ */
/* The streaming player                                                */
/* ------------------------------------------------------------------ */

static int check_player(const BwSdat *sd, u32 seq_index)
{
    BwAudioPlayer *pl = bw_audio_player_create(sd, seq_index, 4.0, 0);
    s16 buf[1024 * 2];
    int i, nonzero = 0;
    long peak = 0;

    BW_CHECK_EQ("the streaming player is created", pl != NULL, 1);
    if (pl == NULL) return 1;
    /* 64 callbacks of 1024 frames is two seconds of the sequence, which is
     * what an SDL audio callback would ask for at this rate. */
    for (i = 0; i < 64; i++) {
        int k;
        bw_audio_player_mix(pl, buf, 1024);
        for (k = 0; k < 1024 * 2; k++) {
            if (buf[k] != 0) nonzero++;
            if (labs(buf[k]) > peak) peak = labs(buf[k]);
        }
    }
    printf("  streaming player: %llu frames, peak %ld, %d non-zero samples, "
           "%u channel overflows\n",
           (unsigned long long)bw_audio_player_frames(pl), peak, nonzero,
           bw_audio_player_voice_overflows(pl));
    BW_CHECK_EQ("the streaming player produced every frame asked for",
                (long)bw_audio_player_frames(pl), 64L * 1024L);
    BW_REQUIRE("the streaming player is not silent", peak > 100);
    BW_CHECK_EQ("no note needed a seventeenth channel",
                (long)bw_audio_player_voice_overflows(pl), 0);
    bw_audio_player_free(pl);
    return 0;
}

/* ------------------------------------------------------------------ */

int main(int argc, char **argv)
{
    const char *data_root;
    const char *oracle_dir;
    char sdat_path[1024];
    char manifest_path[1024];
    BwSdat sd;
    FILE *fh;
    char line[512];
    u32 first_seq = 0;
    int have_first = 0;

    BW_TEST_SETUP(argc, argv);
    data_root = bw_data_root();
    if (argc < 3) {
        printf("usage: test_audio <data root> <oracle dir>\n");
        return 1;
    }
    oracle_dir = argv[2];

    (void)check_py_round();
    (void)check_timer();
    (void)check_chunking();

    sprintf(sdat_path, "%s/wb_sound_data.sdat", data_root);
    if (bw_sdat_load(&sd, sdat_path) != 0) {
        printf("  FAIL cannot load %s\n", sdat_path);
        return 1;
    }
    printf("SDAT: %u SEQ, %u BANK, %u WAVARC, %u PLAYER, %u files\n",
           bw_sdat_count(&sd, BW_SDAT_SEQ), bw_sdat_count(&sd, BW_SDAT_BANK),
           bw_sdat_count(&sd, BW_SDAT_WAVARC), bw_sdat_count(&sd, BW_SDAT_PLAYER),
           sd.fat_count);
    BW_CHECK_EQ("SEQ_BGM_GYM is SEQ 1062",
                bw_sdat_find(&sd, BW_SDAT_SEQ, "SEQ_BGM_GYM"), 1062);

    sprintf(manifest_path, "%s/seqs.txt", oracle_dir);
    fh = fopen(manifest_path, "rb");
    if (fh == NULL) {
        printf("  FAIL cannot open the oracle manifest %s\n", manifest_path);
        bw_sdat_close(&sd);
        return 1;
    }
    while (fgets(line, sizeof(line), fh) != NULL) {
        unsigned seq_index;
        char name[256];
        double max_seconds;
        if (line[0] == '#' || line[0] == '\n') continue;
        if (sscanf(line, "%u %255s %lf", &seq_index, name, &max_seconds) != 3) continue;
        if (!have_first) { first_seq = seq_index; have_first = 1; }
        if (compare_seq(&sd, seq_index, name, max_seconds, oracle_dir) != 0) {
            /* compare_seq already counted and reported the failure. */
        }
    }
    fclose(fh);

    if (have_first) (void)check_player(&sd, first_seq);

    bw_sdat_close(&sd);
    BW_REPORT("test_audio");
}
