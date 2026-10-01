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
    int    tried;
    int    ok;
    BwSwar swar;
} ArcEntry;

typedef struct {
    u32    slot;
    u32    wave_id;
    s16   *samples;
    size_t total_len, loop_start;
    int    loop_flag;
    double native_rate;
} WaveEntry;

/* A region's wave_arc is a 0-3 slot in its bank; the source names the SWAR
 * image in each slot directly, so the cache is keyed by slot. */
typedef struct {
    BwBlob     arc_blob[4];
    int        has_bank;
    BwSbnk     sbnk;
    int        has_sbnk;

    ArcEntry   arcs[4];
    WaveEntry *waves;
    size_t     n_waves, cap_waves;
} BankCtx;

static void bank_ctx_free(BankCtx *c)
{
    size_t i;
    for (i = 0; i < c->n_waves; i++) free(c->waves[i].samples);
    free(c->waves);
    memset(c, 0, sizeof(*c));
}

static void bank_ctx_open(BankCtx *c, const BwAudioSeqSource *src)
{
    int i;
    memset(c, 0, sizeof(*c));
    for (i = 0; i < 4; i++) c->arc_blob[i] = src->wave_arc[i];
    if (src->bank.data == NULL || src->bank.size == 0) return;   /* no bank */
    c->has_bank = 1;
    if (bw_sbnk_open(&c->sbnk, src->bank) != 0) return;
    c->has_sbnk = 1;
}

static const BwSwar *bank_arc(BankCtx *c, u32 slot)
{
    ArcEntry *e = &c->arcs[slot];
    if (!e->tried) {
        e->tried = 1;
        e->ok = c->arc_blob[slot].data != NULL && c->arc_blob[slot].size != 0
                && bw_swar_open(&e->swar, c->arc_blob[slot]) == 0;
    }
    return e->ok ? &e->swar : NULL;
}

/* Resolve a region to a decoded wave, caching the decode.  Returns NULL
 * when the region names no reachable wave, which is a dropped note. */
static const WaveEntry *bank_wave(BankCtx *c, const BwRegion *region)
{
    u32 slot;
    const BwSwar *swar;
    BwSwav w;
    WaveEntry *e;
    size_t i, n;

    if (!c->has_bank) return NULL;
    if (region->wave_arc >= 4) return NULL;
    slot = region->wave_arc;

    for (i = 0; i < c->n_waves; i++)
        if (c->waves[i].slot == slot && c->waves[i].wave_id == region->wave_id)
            return c->waves[i].samples != NULL ? &c->waves[i] : NULL;

    swar = bank_arc(c, slot);
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
    e->slot = slot;
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
/* Sources                                                             */
/* ------------------------------------------------------------------ */

/* A bank record whose SBNK file is missing is a bank with no instruments:
 * every note fails to resolve, which is what the render has always done.
 * One byte that bw_sbnk_open refuses stands for it. */
static const u8 g_no_sbnk[1] = { 0 };

int bw_audio_seq_source_sdat(const BwSdat *s, u32 seq_index, long bank_override,
                             BwAudioSeqSource *out)
{
    BwSdatSeqRecord rec;
    BwSdatBankRecord brec;
    BwSseq q;
    BwBlob blob;
    u32 file_id;
    int i;

    memset(out, 0, sizeof(*out));
    if (bw_sdat_seq_record(s, seq_index, &rec) != 0) {
        fprintf(stderr, "audio: SEQ index %u has no record\n", seq_index);
        return -1;
    }
    out->volume = rec.volume;
    out->bank_index = (bank_override < 0) ? rec.bank : (u32)bank_override;
    if (bw_sdat_bank_record(s, out->bank_index, &brec) == 0) {
        if (bw_sdat_raw_file(s, brec.file_id, &blob) == 0 && blob.size != 0) {
            out->bank = blob;
        } else {
            out->bank.data = g_no_sbnk;
            out->bank.size = sizeof(g_no_sbnk);
        }
        for (i = 0; i < 4; i++) {
            if (brec.wave_arc[i] == 0xFFFF) continue;
            if (bw_sdat_wavarc_fileid(s, brec.wave_arc[i], &file_id) == 0
                && bw_sdat_raw_file(s, file_id, &blob) == 0)
                out->wave_arc[i] = blob;
        }
    }

    if (bw_sdat_raw_file(s, rec.file_id, &blob) != 0) {
        fprintf(stderr, "audio: SEQ %u file id %u is not in the FAT\n",
                seq_index, rec.file_id);
        return -1;
    }
    if (bw_sseq_open(&q, blob) != 0) {
        fprintf(stderr, "audio: SEQ %u is not an SSEQ\n", seq_index);
        return -1;
    }
    /* The oracle's reading: the whole file, offsets file-absolute. */
    out->seq = blob;
    out->seq_start = q.data_offset;
    return 0;
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
    u32               bank_index;
    BankCtx           bank;
    BwSseq            seq;
    BwSeqNoteList     notes;       /* every note event, in oracle order    */
    BwTempoList       tempos;
    TempoSegment     *segs;
    size_t            n_segs;
    int               track_count;
    int               track_no[BW_SSEQ_MAX_TRACKS]; /* by iteration index   */
    int               loops;       /* a track ended at a refused loop jump */
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

static int prepare(const BwAudioSeqSource *src, Prepared *p)
{
    BwSseqTrackStart starts[BW_SSEQ_MAX_TRACKS];
    int n_tracks, t;
    size_t i;

    memset(p, 0, sizeof(*p));
    p->bank_index = src->bank_index;
    bank_ctx_open(&p->bank, src);

    if (src->seq.data == NULL || src->seq_start >= src->seq.size) {
        fprintf(stderr, "audio: the sequence starts outside its bytes\n");
        return -1;
    }
    p->seq.data = src->seq.data;
    p->seq.size = src->seq.size;
    p->seq.data_offset = src->seq_start;

    n_tracks = bw_sseq_track_starts(&p->seq, starts, BW_SSEQ_MAX_TRACKS);
    p->track_count = n_tracks;
    for (t = 0; t < n_tracks; t++) {
        u32 end_tick = 0;
        p->track_no[t] = starts[t].track_no;
        if (bw_sseq_run_track_ex(p->seq.data, p->seq.size, starts[t].start, t,
                                 &p->notes, &p->tempos, &end_tick, &p->loops) != 0)
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

    p->seq_volume_gain = src->volume ? src->volume / 127.0 : 1.0;

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
    BwAudioSeqSource src;
    memset(out, 0, sizeof(*out));
    if (bw_audio_seq_source_sdat(s, seq_index, bank_override, &src) != 0) return -1;
    return bw_audio_render_source(&src, max_seconds, out);
}

int bw_audio_render_source(const BwAudioSeqSource *src, double max_seconds,
                           BwAudioRender *out)
{
    Prepared p;
    double total_seconds, peak = 0.0;
    size_t n_samples, i;
    double *left = NULL, *right = NULL;
    Span *spans = NULL;
    size_t n_spans = 0;
    int notes_rendered = 0, notes_dropped = 0;

    memset(out, 0, sizeof(*out));
    if (prepare(src, &p) != 0) {
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
    u64               tick_base;       /* ticks of the passes already played */
    double            track_gain[BW_SSEQ_MAX_TRACKS];   /* by track number   */
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
    BwAudioSeqSource src;
    if (bw_audio_seq_source_sdat(s, seq_index, -1, &src) != 0) return NULL;
    return bw_audio_player_create_source(&src, max_seconds, loop);
}

BwAudioPlayer *bw_audio_player_create_source(const BwAudioSeqSource *src,
                                             double max_seconds, int loop)
{
    BwAudioPlayer *pl = (BwAudioPlayer *)calloc(1, sizeof(*pl));
    BwAudioRender probe;
    size_t i;
    double total_seconds;

    if (pl == NULL) return NULL;
    if (prepare(src, &pl->p) != 0) {
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
    for (i = 0; i < BW_SSEQ_MAX_TRACKS; i++) pl->track_gain[i] = 1.0;

    /* A streaming mixer cannot peak-normalise a buffer it has not produced
     * yet, so the scale the offline render would apply is measured once,
     * here, and then held for the whole of playback.  That keeps the
     * streamed output at the same level as the rendered file. */
    if (bw_audio_render_source(src, max_seconds, &probe) == 0) {
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

/* The inverse of tick_to_seconds: the last whole tick at or before `t`. */
static u64 seconds_to_tick(const TempoSegment *segs, size_t n, double t)
{
    const TempoSegment *seg;
    size_t i;
    double spt, ticks;

    if (n == 0 || t <= 0.0) return 0;
    seg = &segs[0];
    for (i = 0; i < n; i++) {
        if (segs[i].start_time <= t) seg = &segs[i];
        else break;
    }
    spt = 60.0 / (seg->bpm * (double)BW_AUDIO_TICKS_PER_QUARTER);
    ticks = floor((t - seg->start_time) / spt);
    if (ticks < 0.0) ticks = 0.0;
    return (u64)seg->start_tick + (u64)ticks;
}

static void player_rewind(BwAudioPlayer *p)
{
    int i;
    p->tick_base += seconds_to_tick(p->p.segs, p->p.n_segs,
                                    (double)p->end_sample / BW_AUDIO_RATE);
    p->cursor = 0;
    p->cursor_sample = 0;
    for (i = 0; i < BW_AUDIO_CHANNELS; i++) p->voices[i].active = 0;
}

static int any_voice(const BwAudioPlayer *p)
{
    int i, any = 0;
    for (i = 0; i < BW_AUDIO_CHANNELS; i++) any |= p->voices[i].active;
    return any;
}

int bw_audio_player_finished(const BwAudioPlayer *p)
{
    if (p == NULL) return 1;
    return !p->loop && p->cursor_sample >= p->end_sample
        && p->cursor >= p->n_order && !any_voice(p);
}

int bw_audio_player_seq_loops(const BwAudioPlayer *p)
{
    return p != NULL && p->p.loops;
}

void bw_audio_player_set_loop(BwAudioPlayer *p, int loop)
{
    if (p != NULL) p->loop = loop;
}

u64 bw_audio_player_tick(const BwAudioPlayer *p)
{
    long at;
    if (p == NULL) return 0;
    at = p->cursor_sample < p->end_sample ? p->cursor_sample : p->end_sample;
    return p->tick_base
         + seconds_to_tick(p->p.segs, p->p.n_segs, (double)at / BW_AUDIO_RATE);
}

u32 bw_audio_player_track_mask(const BwAudioPlayer *p)
{
    u32 mask = 0;
    int t;
    if (p == NULL) return 0;
    for (t = 0; t < p->p.track_count; t++)
        if (p->p.track_no[t] >= 0 && p->p.track_no[t] < 32)
            mask |= 1u << p->p.track_no[t];
    return mask;
}

u32 bw_audio_player_voice_mask(const BwAudioPlayer *p)
{
    u32 mask = 0;
    int i;
    if (p == NULL) return 0;
    for (i = 0; i < BW_AUDIO_CHANNELS; i++)
        if (p->voices[i].active) mask |= 1u << i;
    return mask;
}

void bw_audio_player_set_track_gain(BwAudioPlayer *p, int track_no, double gain)
{
    if (p == NULL || track_no < 0 || track_no >= BW_SSEQ_MAX_TRACKS) return;
    p->track_gain[track_no] = gain;
}

void bw_audio_player_render(BwAudioPlayer *p, double *out_l, double *out_r, u32 frames)
{
    /* The block buffer is on the stack and bounded, so the callback never
     * allocates.  A request longer than this is rendered in pieces. */
    enum { BLOCK = 1024 };
    double left[BLOCK], right[BLOCK];
    u32 done = 0;

    if (p == NULL) {
        memset(out_l, 0, (size_t)frames * sizeof(double));
        memset(out_r, 0, (size_t)frames * sizeof(double));
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
            int tn;
            double gain;
            if (start >= block_end) break;
            for (i = 0; i < BW_AUDIO_CHANNELS; i++)
                if (!p->voices[i].active) { slot = i; break; }
            if (slot < 0) {
                p->overflows++;
                p->cursor++;
                continue;
            }
            /* A track gain of exactly 1.0 is not multiplied in, so the
             * default leaves every bit of the note's gain as prepared. */
            gain = inf->gain;
            tn = (inf->ev.track >= 0 && inf->ev.track < p->p.track_count)
                 ? p->p.track_no[inf->ev.track] : -1;
            if (tn >= 0 && tn < BW_SSEQ_MAX_TRACKS && p->track_gain[tn] != 1.0)
                gain *= p->track_gain[tn];
            if (inf->region.kind == BW_REGION_PSG_SQUARE
                || inf->region.kind == BW_REGION_PSG_NOISE) {
                if (bw_audio_voice_start_psg(&p->voices[slot], &inf->region,
                                             inf->ev.note, inf->ev.pan, gain,
                                             inf->start_time, inf->off_time,
                                             BW_AUDIO_RATE) != 0)
                    p->voices[slot].active = 0;
            } else {
                const WaveEntry *w = bank_wave(&p->p.bank, &inf->region);
                if (w == NULL || bw_audio_voice_start_wave(
                        &p->voices[slot], w->samples, w->total_len, w->loop_start,
                        w->loop_flag, w->native_rate, &inf->region, inf->ev.note,
                        inf->ev.pan, gain, inf->start_time, inf->off_time,
                        BW_AUDIO_RATE) != 0)
                    p->voices[slot].active = 0;
            }
            p->cursor++;
        }

        for (i = 0; i < BW_AUDIO_CHANNELS; i++)
            if (p->voices[i].active)
                bw_audio_voice_mix(&p->voices[i], left, right, n, block_start);

        for (k = 0; k < n; k++) {
            out_l[done + k] = left[k] * p->master_gain;
            out_r[done + k] = right[k] * p->master_gain;
        }

        p->cursor_sample = block_end;
        done += n;
        p->frames += n;
        if (p->cursor_sample >= p->end_sample) {
            if (p->loop) player_rewind(p);
            else if (p->cursor >= p->n_order && !any_voice(p)) {
                /* Nothing left to sound: fill the rest with silence. */
                memset(out_l + done, 0, (size_t)(frames - done) * sizeof(double));
                memset(out_r + done, 0, (size_t)(frames - done) * sizeof(double));
                p->frames += frames - done;
                return;
            }
        }
    }
}

void bw_audio_player_mix(BwAudioPlayer *p, s16 *out, u32 frames)
{
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
        if (n > BLOCK) n = BLOCK;
        bw_audio_player_render(p, left, right, n);
        for (k = 0; k < n; k++) {
            double lv = bw_audio_py_round(left[k]);
            double rv = bw_audio_py_round(right[k]);
            if (lv < -32768.0) lv = -32768.0;
            if (lv > 32767.0) lv = 32767.0;
            if (rv < -32768.0) rv = -32768.0;
            if (rv > 32767.0) rv = 32767.0;
            out[(done + k) * 2] = (s16)lv;
            out[(done + k) * 2 + 1] = (s16)rv;
        }
        done += n;
    }
}
