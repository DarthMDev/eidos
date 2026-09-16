/*
 * engine/audio/player.c -- SEQ -> BANK -> WAVARC resolution, the tempo map,
 * the offline render that the oracle test compares against, and the
 * streaming player the platform layer's audio callback drives.
 *
 * SEQ -> BANK is the *direct* u16 at the SEQ INFO record's +4 (aletheia's
 * tools/assets/sdat.py settled this: 940 of 940 populated SEQ records
 * resolve to a real BANK record that way and the names line up one to one;
 * the PLAYER record carries no bank reference at all).  A region's wave_arc
 * is a local 0-3 slot in the owning BANK's own wave_arc[4] table, which
 * supplies the global WAVARC index, whose INFO record is a bare file id for
 * the SWAR archive; wave_id then selects the SWAV inside it.
 *
 * The tempo map is 48 ticks to the quarter (the Nitro Composer convention
 * the asset lane's sseq2midi.py and sseqsynth.py both assume), piecewise
 * over every Tempo event any track emitted, with 120 BPM before the first.
 *
 * Two things here exist only so the comparison against the oracle can be
 * exact rather than approximate, and both are properties of floating point
 * rather than of audio:
 *
 *   - the track iteration order is the oracle's, which is a Python dict's
 *     insertion order: the OpenTrack order with track 0 appended last.
 *     It fixes the order of the note list.
 *   - the offline render sums the notes in that order, each note's whole
 *     span at once, because floating point addition is not associative.
 *     The streaming player sums per block in voice order instead; that is
 *     a different order and may differ in the last ulp, which is why the
 *     test compares the offline path.
 */

#include "audio.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* The bank context: the SBNK, the SWAR cache and the decode cache      */
/* ------------------------------------------------------------------ */

typedef struct {
    u32    arc;
    int    ok;
    BwSwar swar;
} ArcEntry;

typedef struct {
    u32    arc;
    u32    wave_id;
    s16   *samples;
    size_t total_len, loop_start;
    int    loop_flag;
    double native_rate;
} WaveEntry;

typedef struct {
    const BwSdat    *sdat;
    BwSdatBankRecord brec;
    int              has_bank;
    BwSbnk           sbnk;
    int              has_sbnk;

    ArcEntry  *arcs;
    size_t     n_arcs, cap_arcs;
    WaveEntry *waves;
    size_t     n_waves, cap_waves;
} BankCtx;

static void bank_ctx_free(BankCtx *c)
{
    size_t i;
    for (i = 0; i < c->n_waves; i++) free(c->waves[i].samples);
    free(c->waves);
    free(c->arcs);
    memset(c, 0, sizeof(*c));
}

static int bank_ctx_open(BankCtx *c, const BwSdat *s, u32 bank_index)
{
    BwBlob blob;
    memset(c, 0, sizeof(*c));
    c->sdat = s;
    if (bw_sdat_bank_record(s, bank_index, &c->brec) != 0) return 0;  /* no bank */
    c->has_bank = 1;
    if (bw_sdat_raw_file(s, c->brec.file_id, &blob) != 0) return 0;
    if (bw_sbnk_open(&c->sbnk, blob) != 0) return 0;
    c->has_sbnk = 1;
    return 0;
}

static const BwSwar *bank_arc(BankCtx *c, u32 global_arc)
{
    size_t i;
    ArcEntry *e;
    u32 file_id;
    BwBlob blob;

    for (i = 0; i < c->n_arcs; i++)
        if (c->arcs[i].arc == global_arc)
            return c->arcs[i].ok ? &c->arcs[i].swar : NULL;

    if (c->n_arcs == c->cap_arcs) {
        size_t cap = c->cap_arcs ? c->cap_arcs * 2 : 8;
        ArcEntry *v = (ArcEntry *)realloc(c->arcs, cap * sizeof(*v));
        if (v == NULL) return NULL;
        c->arcs = v;
        c->cap_arcs = cap;
    }
    e = &c->arcs[c->n_arcs++];
    e->arc = global_arc;
    e->ok = 0;
    if (bw_sdat_wavarc_fileid(c->sdat, global_arc, &file_id) == 0
        && bw_sdat_raw_file(c->sdat, file_id, &blob) == 0
        && bw_swar_open(&e->swar, blob) == 0) {
        e->ok = 1;
    }
    return e->ok ? &e->swar : NULL;
}

/* Resolve a region to a decoded wave, caching the decode.  Returns NULL
 * when the region names no reachable wave, which is a dropped note. */
static const WaveEntry *bank_wave(BankCtx *c, const BwRegion *region)
{
    u32 global_arc;
    const BwSwar *swar;
    BwSwav w;
    WaveEntry *e;
    size_t i, n;

    if (!c->has_bank) return NULL;
    if (region->wave_arc >= 4) return NULL;
    global_arc = c->brec.wave_arc[region->wave_arc];
    if (global_arc == 0xFFFF) return NULL;

    for (i = 0; i < c->n_waves; i++)
        if (c->waves[i].arc == global_arc && c->waves[i].wave_id == region->wave_id)
            return c->waves[i].samples != NULL ? &c->waves[i] : NULL;

    swar = bank_arc(c, global_arc);
    if (swar == NULL) return NULL;
    if (bw_swar_wave(swar, region->wave_id, &w) != 0) return NULL;

    if (c->n_waves == c->cap_waves) {
        size_t cap = c->cap_waves ? c->cap_waves * 2 : 16;
        WaveEntry *v = (WaveEntry *)realloc(c->waves, cap * sizeof(*v));
        if (v == NULL) return NULL;
        c->waves = v;
        c->cap_waves = cap;
    }
    e = &c->waves[c->n_waves++];
    memset(e, 0, sizeof(*e));
    e->arc = global_arc;
    e->wave_id = region->wave_id;

    n = bw_swav_decoded_count(&w);
    if (n == 0) return NULL;
    e->samples = (s16 *)malloc(n * sizeof(s16));
    if (e->samples == NULL) return NULL;
    e->total_len = bw_swav_decode(&w, e->samples, n);
    e->loop_start = bw_swav_loop_start(&w, e->total_len);
    e->loop_flag = w.loop_flag;
    e->native_rate = w.sample_rate ? (double)w.sample_rate : 16000.0;
    return e;
}

/* ------------------------------------------------------------------ */
/* Tempo map                                                           */
/* ------------------------------------------------------------------ */

typedef struct {
    u32    start_tick;
    double start_time;
    int    bpm;
} TempoSegment;

static int tempo_cmp(const void *a, const void *b)
{
    const BwTempoEvent *x = (const BwTempoEvent *)a;
    const BwTempoEvent *y = (const BwTempoEvent *)b;
    if (x->tick != y->tick) return x->tick < y->tick ? -1 : 1;
    if (x->bpm != y->bpm) return x->bpm < y->bpm ? -1 : 1;
    return 0;
}

/* sorted(set(events)) then the piecewise walk, exactly as
 * sseqsynth.py's build_tempo_map(). */
static size_t build_tempo_map(const BwTempoList *all, TempoSegment **out_segs)
{
    BwTempoEvent *ev;
    size_t n = 0, i, cap;
    TempoSegment *segs;
    size_t n_segs = 0;
    u32 cur_tick = 0;
    double cur_time = 0.0;
    int cur_bpm;

    cap = all->n + 1;
    ev = (BwTempoEvent *)malloc(cap * sizeof(*ev));
    if (ev == NULL) return 0;
    if (all->n > 0) {
        memcpy(ev, all->v, all->n * sizeof(*ev));
        qsort(ev, all->n, sizeof(*ev), tempo_cmp);
        for (i = 0; i < all->n; i++) {
            if (n > 0 && ev[i].tick == ev[n - 1].tick && ev[i].bpm == ev[n - 1].bpm)
                continue;
            ev[n++] = ev[i];
        }
    }
    if (n == 0 || ev[0].tick != 0) {
        memmove(ev + 1, ev, n * sizeof(*ev));
        ev[0].tick = 0;
        ev[0].bpm = BW_AUDIO_DEFAULT_BPM;
        n++;
    }

    segs = (TempoSegment *)malloc(n * sizeof(*segs));
    if (segs == NULL) { free(ev); return 0; }
    cur_bpm = ev[0].bpm;
    for (i = 1; i < n; i++) {
        double spt;
        if (ev[i].tick == cur_tick) { cur_bpm = ev[i].bpm; continue; }
        segs[n_segs].start_tick = cur_tick;
        segs[n_segs].start_time = cur_time;
        segs[n_segs].bpm = cur_bpm;
        n_segs++;
        spt = 60.0 / (cur_bpm * (double)BW_AUDIO_TICKS_PER_QUARTER);
        cur_time += (ev[i].tick - cur_tick) * spt;
        cur_tick = ev[i].tick;
        cur_bpm = ev[i].bpm;
    }
    segs[n_segs].start_tick = cur_tick;
    segs[n_segs].start_time = cur_time;
    segs[n_segs].bpm = cur_bpm;
    n_segs++;
    free(ev);
    *out_segs = segs;
    return n_segs;
}

static double tick_to_seconds(const TempoSegment *segs, size_t n, u32 tick)
{
    const TempoSegment *seg = &segs[0];
    size_t i;
    double spt;
    for (i = 0; i < n; i++) {
        if (segs[i].start_tick <= tick) seg = &segs[i];
        else break;
    }
    spt = 60.0 / (seg->bpm * (double)BW_AUDIO_TICKS_PER_QUARTER);
    return seg->start_time + (tick - seg->start_tick) * spt;
}

/* ------------------------------------------------------------------ */
/* Preparing a sequence                                                */
/* ------------------------------------------------------------------ */

typedef struct {
    BwSdatSeqRecord   rec;
    u32               bank_index;
    BankCtx           bank;
    BwSseq            seq;
    BwBlob            seq_blob;
    BwSeqNoteList     notes;       /* every note event, in oracle order    */
    BwTempoList       tempos;
    TempoSegment     *segs;
    size_t            n_segs;
    int               track_count;
    int               master_bpm;
    double            seq_volume_gain;
    BwAudioNoteInfo  *info;        /* one per note event                   */
    size_t            n_info;
    size_t            n_prepared;  /* those with a resolved region         */
    double            max_end_time;
} Prepared;

static void prepared_free(Prepared *p)
{
    bw_seq_note_list_free(&p->notes);
    bw_tempo_list_free(&p->tempos);
    free(p->segs);
    free(p->info);
    bank_ctx_free(&p->bank);
    memset(p, 0, sizeof(*p));
}

static int prepare(const BwSdat *s, u32 seq_index, long bank_override, Prepared *p)
{
    BwSseqTrackStart starts[BW_SSEQ_MAX_TRACKS];
    int n_tracks, t;
    size_t i;
    BwBlob blob;

    memset(p, 0, sizeof(*p));
    if (bw_sdat_seq_record(s, seq_index, &p->rec) != 0) {
        fprintf(stderr, "audio: SEQ index %u has no record\n", seq_index);
        return -1;
    }
    p->bank_index = (bank_override < 0) ? p->rec.bank : (u32)bank_override;
    bank_ctx_open(&p->bank, s, p->bank_index);

    if (bw_sdat_raw_file(s, p->rec.file_id, &blob) != 0) {
        fprintf(stderr, "audio: SEQ %u file id %u is not in the FAT\n",
                seq_index, p->rec.file_id);
        return -1;
    }
    p->seq_blob = blob;
    if (bw_sseq_open(&p->seq, blob) != 0) {
        fprintf(stderr, "audio: SEQ %u is not an SSEQ\n", seq_index);
        return -1;
    }

    n_tracks = bw_sseq_track_starts(&p->seq, starts, BW_SSEQ_MAX_TRACKS);
    p->track_count = n_tracks;
    for (t = 0; t < n_tracks; t++) {
        u32 end_tick = 0;
        if (bw_sseq_run_track(p->seq.data, p->seq.size, starts[t].start, t,
                              &p->notes, &p->tempos, &end_tick) != 0)
            return -1;
    }

    p->n_segs = build_tempo_map(&p->tempos, &p->segs);
    if (p->n_segs == 0) return -1;
    p->master_bpm = p->segs[0].bpm;
    if (p->tempos.n > 0) {
        /* sorted(all_tempo_events)[0][1]: the bpm of the earliest event,
         * with the lowest bpm winning a tie at the same tick. */
        size_t best = 0;
        for (i = 1; i < p->tempos.n; i++) {
            if (p->tempos.v[i].tick < p->tempos.v[best].tick
                || (p->tempos.v[i].tick == p->tempos.v[best].tick
                    && p->tempos.v[i].bpm < p->tempos.v[best].bpm))
                best = i;
        }
        p->master_bpm = p->tempos.v[best].bpm;
    }

    p->seq_volume_gain = p->rec.volume ? p->rec.volume / 127.0 : 1.0;

    p->n_info = p->notes.n;
    p->info = (BwAudioNoteInfo *)calloc(p->n_info ? p->n_info : 1, sizeof(*p->info));
    if (p->info == NULL) return -1;
    for (i = 0; i < p->n_info; i++) {
        const BwSeqNoteEvent *n = &p->notes.v[i];
        BwAudioNoteInfo *inf = &p->info[i];
        inf->ev = *n;
        inf->start_time = tick_to_seconds(p->segs, p->n_segs, n->tick);
        inf->off_time = tick_to_seconds(p->segs, p->n_segs, n->tick + n->dur);
        inf->resolved = 0;
        if (p->bank.has_sbnk
            && bw_sbnk_resolve(&p->bank.sbnk, n->program, n->note, &inf->region) == 0) {
            inf->resolved = 1;
            inf->gain = p->seq_volume_gain * (n->volume / 127.0)
                      * (n->expression / 127.0) * (n->velocity / 127.0);
            p->n_prepared++;
            if (inf->off_time > p->max_end_time) p->max_end_time = inf->off_time;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* The offline render                                                  */
/* ------------------------------------------------------------------ */

typedef struct { long start, end; } Span;

static int long_cmp(const void *a, const void *b)
{
    long x = *(const long *)a, y = *(const long *)b;
    return x < y ? -1 : (x > y ? 1 : 0);
}

/* How many notes were ever sounding at once, which is what says whether the
 * DS's 16 hardware channels would have been enough for this sequence.  A
 * two-pointer sweep over the sorted start and end times. */
static int max_concurrency(const Span *spans, size_t n)
{
    long *starts, *ends;
    size_t i = 0, j = 0;
    int live = 0, best = 0;

    if (n == 0) return 0;
    starts = (long *)malloc(n * sizeof(long));
    ends = (long *)malloc(n * sizeof(long));
    if (starts == NULL || ends == NULL) { free(starts); free(ends); return 0; }
    for (i = 0; i < n; i++) { starts[i] = spans[i].start; ends[i] = spans[i].end; }
    qsort(starts, n, sizeof(long), long_cmp);
    qsort(ends, n, sizeof(long), long_cmp);
    i = j = 0;
    while (i < n) {
        if (starts[i] < ends[j]) {
            live++;
            if (live > best) best = live;
            i++;
        } else {
            live--;
            j++;
        }
    }
    free(starts);
    free(ends);
    return best;
}

int bw_audio_render_seq(const BwSdat *s, u32 seq_index, long bank_override,
                        double max_seconds, BwAudioRender *out)
{
    Prepared p;
    double total_seconds, peak = 0.0;
    size_t n_samples, i;
    double *left = NULL, *right = NULL;
    Span *spans = NULL;
    size_t n_spans = 0;
    int notes_rendered = 0, notes_dropped = 0;

    memset(out, 0, sizeof(*out));
    if (prepare(s, seq_index, bank_override, &p) != 0) {
        prepared_free(&p);
        return -1;
    }

    if (p.n_prepared > 0) {
        total_seconds = p.max_end_time + BW_AUDIO_TAIL_PAD_SECONDS;
        if (max_seconds < total_seconds) total_seconds = max_seconds;
    } else {
        total_seconds = 0.5;
    }
    if (total_seconds < 0.25) total_seconds = 0.25;
    n_samples = (size_t)ceil(total_seconds * BW_AUDIO_RATE);

    left = (double *)calloc(n_samples, sizeof(double));
    right = (double *)calloc(n_samples, sizeof(double));
    spans = (Span *)calloc(p.n_info ? p.n_info : 1, sizeof(Span));
    if (left == NULL || right == NULL || spans == NULL) {
        free(left); free(right); free(spans);
        prepared_free(&p);
        return -1;
    }

    for (i = 0; i < p.n_info; i++) {
        BwAudioNoteInfo *inf = &p.info[i];
        BwAudioVoice v;
        if (!inf->resolved) continue;                 /* never prepared */
        if (inf->start_time * BW_AUDIO_RATE >= (double)n_samples) {
            notes_dropped++;
            continue;
        }
        if (inf->region.kind == BW_REGION_PSG_SQUARE
            || inf->region.kind == BW_REGION_PSG_NOISE) {
            if (bw_audio_voice_start_psg(&v, &inf->region, inf->ev.note, inf->ev.pan,
                                         inf->gain, inf->start_time, inf->off_time,
                                         BW_AUDIO_RATE) != 0) {
                notes_dropped++;
                continue;
            }
        } else {
            const WaveEntry *w = bank_wave(&p.bank, &inf->region);
            if (w == NULL) { notes_dropped++; continue; }
            if (bw_audio_voice_start_wave(&v, w->samples, w->total_len, w->loop_start,
                                          w->loop_flag, w->native_rate, &inf->region,
                                          inf->ev.note, inf->ev.pan, inf->gain,
                                          inf->start_time, inf->off_time,
                                          BW_AUDIO_RATE) != 0) {
                notes_dropped++;
                continue;
            }
        }
        spans[n_spans].start = v.start_sample;
        spans[n_spans].end = v.start_sample + (long)v.n_out;
        n_spans++;
        /* One call for the whole buffer: the oracle mixes each note's whole
         * span before moving to the next, and the sum order is observable. */
        bw_audio_voice_mix(&v, left, right, n_samples, 0);
        inf->rendered = 1;
        notes_rendered++;
    }

    for (i = 0; i < n_samples; i++) {
        double a = fabs(left[i]);
        if (a > peak) peak = a;
    }
    for (i = 0; i < n_samples; i++) {
        double a = fabs(right[i]);
        if (a > peak) peak = a;
    }
    if (peak > 32000.0) {
        double scale = 32000.0 / peak;
        for (i = 0; i < n_samples; i++) { left[i] *= scale; right[i] *= scale; }
        peak = 32000.0;
    }

    {
        size_t fade = (size_t)(BW_AUDIO_FADE_SECONDS * BW_AUDIO_RATE);
        if (fade > n_samples) fade = n_samples;
        for (i = 0; i < fade; i++) {
            size_t idx = n_samples - fade + i;
            double mult = 1.0 - ((double)i / (double)fade);
            left[idx] *= mult;
            right[idx] *= mult;
        }
    }

    out->pcm = (s16 *)malloc(n_samples * 2 * sizeof(s16));
    if (out->pcm == NULL) {
        free(left); free(right); free(spans);
        prepared_free(&p);
        return -1;
    }
    for (i = 0; i < n_samples; i++) {
        double lv = bw_audio_py_round(left[i]);
        double rv = bw_audio_py_round(right[i]);
        if (lv < -32768.0) lv = -32768.0;
        if (lv > 32767.0) lv = 32767.0;
        if (rv < -32768.0) rv = -32768.0;
        if (rv > 32767.0) rv = 32767.0;
        out->pcm[i * 2] = (s16)lv;
        out->pcm[i * 2 + 1] = (s16)rv;
    }

    out->n_samples = n_samples;
    out->duration_seconds = total_seconds;
    out->peak_amplitude = peak;
    out->master_bpm = p.master_bpm;
    out->bank_index = p.bank_index;
    out->track_count = p.track_count;
    out->note_events_total = (int)p.n_info;
    out->notes_rendered = notes_rendered;
    out->notes_dropped = notes_dropped;
    out->max_concurrent = max_concurrency(spans, n_spans);
    out->notes = p.info;
    out->note_count = p.n_info;
    p.info = NULL;              /* ownership moves to the caller */

    free(left);
    free(right);
    free(spans);
    prepared_free(&p);
    return 0;
}

void bw_audio_render_free(BwAudioRender *r)
{
    if (r == NULL) return;
    free(r->pcm);
    free(r->notes);
    memset(r, 0, sizeof(*r));
}

/* ------------------------------------------------------------------ */
/* The streaming player                                                */
/* ------------------------------------------------------------------ */

struct BwAudioPlayer {
    Prepared p;
    /* prepared notes in start order, which is the order a streaming mixer
     * has to start them in */
    BwAudioNoteInfo **order;
    size_t            n_order, cursor;
    BwAudioVoice      voices[BW_AUDIO_CHANNELS];
    long              cursor_sample;
    long              end_sample;
    int               loop;
    double            master_gain;
    u32               overflows;
    u64               frames;
};

static int order_cmp(const void *a, const void *b)
{
    const BwAudioNoteInfo *x = *(const BwAudioNoteInfo * const *)a;
    const BwAudioNoteInfo *y = *(const BwAudioNoteInfo * const *)b;
    if (x->start_time < y->start_time) return -1;
    if (x->start_time > y->start_time) return 1;
    return 0;
}

BwAudioPlayer *bw_audio_player_create(const BwSdat *s, u32 seq_index,
                                      double max_seconds, int loop)
{
    BwAudioPlayer *pl = (BwAudioPlayer *)calloc(1, sizeof(*pl));
    BwAudioRender probe;
    size_t i;
    double total_seconds;

    if (pl == NULL) return NULL;
    if (prepare(s, seq_index, -1, &pl->p) != 0) {
        prepared_free(&pl->p);
        free(pl);
        return NULL;
    }
    pl->order = (BwAudioNoteInfo **)calloc(pl->p.n_info ? pl->p.n_info : 1,
                                           sizeof(*pl->order));
    if (pl->order == NULL) {
        prepared_free(&pl->p);
        free(pl);
        return NULL;
    }
    for (i = 0; i < pl->p.n_info; i++)
        if (pl->p.info[i].resolved) pl->order[pl->n_order++] = &pl->p.info[i];
    qsort(pl->order, pl->n_order, sizeof(*pl->order), order_cmp);

    total_seconds = pl->p.n_prepared ? pl->p.max_end_time + BW_AUDIO_TAIL_PAD_SECONDS
                                     : 0.5;
    if (total_seconds > max_seconds) total_seconds = max_seconds;
    if (total_seconds < 0.25) total_seconds = 0.25;
    pl->end_sample = (long)ceil(total_seconds * BW_AUDIO_RATE);
    pl->loop = loop;
    pl->master_gain = 1.0;

    /* A streaming mixer cannot peak-normalise a buffer it has not produced
     * yet, so the scale the offline render would apply is measured once,
     * here, and then held for the whole of playback.  That keeps the
     * streamed output at the same level as the rendered file. */
    if (bw_audio_render_seq(s, seq_index, -1, max_seconds, &probe) == 0) {
        if (probe.peak_amplitude > 32000.0)
            pl->master_gain = 32000.0 / probe.peak_amplitude;
        bw_audio_render_free(&probe);
    }
    return pl;
}

void bw_audio_player_free(BwAudioPlayer *p)
{
    if (p == NULL) return;
    free(p->order);
    prepared_free(&p->p);
    free(p);
}

u32 bw_audio_player_voice_overflows(const BwAudioPlayer *p)
{
    return p == NULL ? 0 : p->overflows;
}

u64 bw_audio_player_frames(const BwAudioPlayer *p)
{
    return p == NULL ? 0 : p->frames;
}

static void player_rewind(BwAudioPlayer *p)
{
    int i;
    p->cursor = 0;
    p->cursor_sample = 0;
    for (i = 0; i < BW_AUDIO_CHANNELS; i++) p->voices[i].active = 0;
}

void bw_audio_player_mix(BwAudioPlayer *p, s16 *out, u32 frames)
{
    /* The block buffer is on the stack and bounded, so the callback never
     * allocates.  A request longer than this is rendered in pieces. */
    enum { BLOCK = 1024 };
    double left[BLOCK], right[BLOCK];
    u32 done = 0;

    if (p == NULL) {
        memset(out, 0, (size_t)frames * 2 * sizeof(s16));
        return;
    }
    while (done < frames) {
        u32 n = frames - done;
        u32 k;
        int i;
        long block_start, block_end;

        if (n > BLOCK) n = BLOCK;
        memset(left, 0, n * sizeof(double));
        memset(right, 0, n * sizeof(double));
        block_start = p->cursor_sample;
        block_end = block_start + (long)n;

        while (p->cursor < p->n_order) {
            BwAudioNoteInfo *inf = p->order[p->cursor];
            long start = (long)bw_audio_py_round(inf->start_time * BW_AUDIO_RATE);
            int slot = -1;
            if (start >= block_end) break;
            for (i = 0; i < BW_AUDIO_CHANNELS; i++)
                if (!p->voices[i].active) { slot = i; break; }
            if (slot < 0) {
                p->overflows++;
                p->cursor++;
                continue;
            }
            if (inf->region.kind == BW_REGION_PSG_SQUARE
                || inf->region.kind == BW_REGION_PSG_NOISE) {
                if (bw_audio_voice_start_psg(&p->voices[slot], &inf->region,
                                             inf->ev.note, inf->ev.pan, inf->gain,
                                             inf->start_time, inf->off_time,
                                             BW_AUDIO_RATE) != 0)
                    p->voices[slot].active = 0;
            } else {
                const WaveEntry *w = bank_wave(&p->p.bank, &inf->region);
                if (w == NULL || bw_audio_voice_start_wave(
                        &p->voices[slot], w->samples, w->total_len, w->loop_start,
                        w->loop_flag, w->native_rate, &inf->region, inf->ev.note,
                        inf->ev.pan, inf->gain, inf->start_time, inf->off_time,
                        BW_AUDIO_RATE) != 0)
                    p->voices[slot].active = 0;
            }
            p->cursor++;
        }

        for (i = 0; i < BW_AUDIO_CHANNELS; i++)
            if (p->voices[i].active)
                bw_audio_voice_mix(&p->voices[i], left, right, n, block_start);

        for (k = 0; k < n; k++) {
            double lv = bw_audio_py_round(left[k] * p->master_gain);
            double rv = bw_audio_py_round(right[k] * p->master_gain);
            if (lv < -32768.0) lv = -32768.0;
            if (lv > 32767.0) lv = 32767.0;
            if (rv < -32768.0) rv = -32768.0;
            if (rv > 32767.0) rv = 32767.0;
            out[(done + k) * 2] = (s16)lv;
            out[(done + k) * 2 + 1] = (s16)rv;
        }

        p->cursor_sample = block_end;
        done += n;
        p->frames += n;
        if (p->cursor_sample >= p->end_sample) {
            if (p->loop) player_rewind(p);
            else if (p->cursor >= p->n_order) {
                int any = 0;
                for (i = 0; i < BW_AUDIO_CHANNELS; i++) any |= p->voices[i].active;
                if (!any) {
                    /* Nothing left to sound: fill the rest with silence. */
                    memset(out + (size_t)done * 2, 0,
                           (size_t)(frames - done) * 2 * sizeof(s16));
                    p->frames += frames - done;
                    return;
                }
            }
        }
    }
}
