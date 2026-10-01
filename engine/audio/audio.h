/*
 * engine/audio/audio.h -- the SDAT sequencer and mixer, in pure C.
 *
 * This is the host-side stand-in for the DS's ARM7 sound core: the SDAT
 * container, the SBNK instrument tables, the SWAR/SWAV wave archives with
 * PCM8/PCM16/IMA-ADPCM decode, the SSEQ bytecode interpreter and a 16
 * channel mixer at 32768 Hz.  No SDL, no third-party code, standard
 * library only (math.h for pow/fmod/floor and nothing else).
 *
 * Provenance of every rule implemented here:
 *
 *   1. aletheia's asset lane, which established these formats against the
 *      ROM's own bytes and documents each one in the tool that reads it:
 *      tools/assets/sdat.py (SYMB/INFO/FAT/FILE, the SEQ record's direct
 *      u16 BANK index at +4, the BANK record's four wave-archive slots,
 *      the WAVARC record's bare u32 file id, the PLAYER record),
 *      tools/assets/sbnk.py (the 4-byte instrument-table entry, the
 *      10-byte region record and the type 0/1/2/3/16/17 shapes),
 *      tools/assets/swav.py (the SWAR wave-offset table and the SWAV
 *      header), tools/assets/adpcm.py (the IMA step and index tables) and
 *      tools/assets/sseq.py (the opcode table).
 *   2. tools/assets/sseqsynth.py, the reference softsynth, which is the
 *      *oracle* this engine is tested against: SEQ -> BANK resolution, the
 *      track interpreter's jump/call/return control flow, the treatment of
 *      the three undocumented opcodes 0xA0/0xA1/0xA2, the tempo map at 48
 *      ticks per quarter, the linear ADSR, the semitone-ratio resample and
 *      the linear pan.
 *   3. public NDS sound hardware documentation for the channel timer
 *      formula (timer = -(clock / frequency), clock = 33513982/2 Hz), the
 *      PSG pulse channel's eight duty settings and the 15-bit noise LFSR.
 *
 * Nothing here is derived from any other decompilation, port or emulator.
 *
 * --------------------------------------------------------------------
 * Why this engine computes in double rather than in fixed point
 * --------------------------------------------------------------------
 * The oracle is a Python program and Python floats are IEEE-754 binary64.
 * Computing the same quantities in the same order in C `double` makes the
 * comparison *exact* rather than approximate, which is a far stronger test
 * than any tolerance: tests/test_audio.c requires bit-identical note event
 * lists and reports the largest sample difference, which is 0 for the three
 * reference sequences.  Two places need care and both are handled here:
 *
 *   - Python's round() is round-half-to-even, not C round()'s half-away-
 *     from-zero.  bw_audio_py_round() below is CPython's own float.__round__
 *     with no ndigits: round(x), and if |x - round(x)| == 0.5 exactly then
 *     2 * round(x / 2).
 *   - the order of the mix accumulation is observable, because floating
 *     point addition is not associative.  bw_audio_render_seq() sums the
 *     notes in exactly the oracle's order (track iteration order, then
 *     note order within a track, each note's whole span at once).  The
 *     streaming player below sums per block in voice order instead, which
 *     is a different order and therefore may differ in the last ulp; that
 *     path is not the one the oracle test compares.
 *
 * Nothing about the engine *requires* double: BwAudioVoice is the whole of
 * the per-note arithmetic and is the piece a later fixed-point pass would
 * replace, with this test standing as the reference it has to stay near.
 */

#ifndef EIDOS_ENGINE_AUDIO_H
#define EIDOS_ENGINE_AUDIO_H

#include <stddef.h>

#include "types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The DS mixer's own output rate, which is what engine/platform opens the
 * host audio device at, so nothing resamples twice. */
#define BW_AUDIO_RATE          32768

/* Hardware channels.  The DS sound core has 16; the streaming player
 * allocates one voice per sounding note out of that many and counts the
 * notes it had to drop. */
#define BW_AUDIO_CHANNELS      16

/* sseqsynth.py's own constants, reproduced so the two agree by
 * construction rather than by coincidence. */
#define BW_AUDIO_TICKS_PER_QUARTER 48
#define BW_AUDIO_DEFAULT_BPM       120
#define BW_AUDIO_TAIL_PAD_SECONDS  1.5
#define BW_AUDIO_FADE_SECONDS      1.0
#define BW_AUDIO_ATTACK_MAX        0.15
#define BW_AUDIO_DECAY_MAX         0.30
#define BW_AUDIO_RELEASE_MAX       0.40
#define BW_AUDIO_MIN_RELEASE       0.02
#define BW_AUDIO_MAX_NOTE_SECONDS  12.0
#define BW_AUDIO_MAX_STEPS_PER_TRACK 300000
#define BW_AUDIO_MAX_JUMP_REPEATS  1
#define BW_AUDIO_MAX_CALL_DEPTH    32

/* The channel timer clock: the 16-bit per-channel TIMER register counts at
 * half the ARM7 bus clock.  Public NDS sound hardware documentation. */
#define BW_AUDIO_TIMER_CLOCK_HZ (33513982.0 / 2.0)

/* ------------------------------------------------------------------ */
/* Blobs                                                               */
/* ------------------------------------------------------------------ */

typedef struct {
    const u8 *data;
    size_t    size;
} BwBlob;

/* ------------------------------------------------------------------ */
/* SDAT                                                                */
/* ------------------------------------------------------------------ */

typedef enum {
    BW_SDAT_SEQ = 0,
    BW_SDAT_SEQARC,
    BW_SDAT_BANK,
    BW_SDAT_WAVARC,
    BW_SDAT_PLAYER,
    BW_SDAT_GROUP,
    BW_SDAT_PLAYER2,
    BW_SDAT_STRM,
    BW_SDAT_KIND_COUNT
} BwSdatKind;

typedef struct {
    u32 file_id;
    u16 bank;
    u8  volume;
    u8  channel_pressure;
    u8  player_priority;
    u8  player_no;
} BwSdatSeqRecord;

typedef struct {
    u16 file_id;
    u16 wave_arc[4];       /* global WAVARC indices, 0xFFFF = unused slot */
} BwSdatBankRecord;

typedef struct {
    u8  seq_max;
    u16 channel_bitmask;
    u32 heap_size;
} BwSdatPlayerRecord;

typedef struct {
    const u8 *data;        /* the whole SDAT image                       */
    size_t    size;
    u8       *owned;       /* non-NULL when bw_sdat_load() read the file  */

    const u8 *info;        /* the INFO block body                        */
    size_t    info_size;
    const u8 *symb;        /* the SYMB block body, or NULL               */
    size_t    symb_size;

    u32       count[BW_SDAT_KIND_COUNT];
    u32       rec_table[BW_SDAT_KIND_COUNT];  /* INFO-relative offset, 0 = none */
    u32       symb_table[BW_SDAT_KIND_COUNT]; /* SYMB-relative offset, 0 = none */

    const u8 *fat;         /* the FAT block body                         */
    u32       fat_count;
} BwSdat;

/* Parse an SDAT image the caller owns.  Returns 0, or -1 with a message on
 * stderr.  The BwSdat borrows `data` and does not copy it. */
int  bw_sdat_open(BwSdat *s, const void *data, size_t size);
/* Read the file at `path` and parse it; the BwSdat then owns the buffer. */
int  bw_sdat_load(BwSdat *s, const char *path);
void bw_sdat_close(BwSdat *s);

u32  bw_sdat_count(const BwSdat *s, BwSdatKind kind);
/* The SYMB name of a slot, or NULL when there is no SYMB block or no name. */
const char *bw_sdat_name(const BwSdat *s, BwSdatKind kind, u32 index);
/* The slot whose SYMB name is `name`, or -1. */
long bw_sdat_find(const BwSdat *s, BwSdatKind kind, const char *name);

int  bw_sdat_seq_record(const BwSdat *s, u32 index, BwSdatSeqRecord *out);
int  bw_sdat_bank_record(const BwSdat *s, u32 index, BwSdatBankRecord *out);
int  bw_sdat_player_record(const BwSdat *s, u32 index, BwSdatPlayerRecord *out);
int  bw_sdat_wavarc_fileid(const BwSdat *s, u32 index, u32 *out);
int  bw_sdat_raw_file(const BwSdat *s, u32 file_id, BwBlob *out);

/* ------------------------------------------------------------------ */
/* SBNK                                                                */
/* ------------------------------------------------------------------ */

typedef enum {
    BW_REGION_NONE = 0,
    BW_REGION_WAVE,
    BW_REGION_PSG_SQUARE,
    BW_REGION_PSG_NOISE
} BwRegionKind;

typedef struct {
    BwRegionKind kind;
    u16 wave_id;      /* BW_REGION_WAVE only                              */
    u16 wave_arc;     /* index into the owning BANK's wave_arc[4]         */
    u16 duty;         /* BW_REGION_PSG_SQUARE only, 0-7                   */
    u8  base_note;
    u8  attack, decay, sustain, release;
    u8  pan;
} BwRegion;

typedef struct {
    const u8 *data;
    size_t    size;
    u32       instrument_count;
} BwSbnk;

int bw_sbnk_open(BwSbnk *b, BwBlob blob);
/* The instrument-table entry's record type (0/1/2/3/16/17), or -1. */
int bw_sbnk_instrument_type(const BwSbnk *b, u32 program);
/* Resolve `program` at `played_note` to one region, exactly as
 * sseqsynth.py's resolve_instrument_region() does.  Returns 0 on success,
 * -1 when the program has no playable region (type 0, out of range, a
 * record that runs past the end of the file). */
int bw_sbnk_resolve(const BwSbnk *b, int program, int played_note, BwRegion *out);

/* ------------------------------------------------------------------ */
/* SWAR / SWAV                                                         */
/* ------------------------------------------------------------------ */

#define BW_WAVE_PCM8   0
#define BW_WAVE_PCM16  1
#define BW_WAVE_ADPCM  2

typedef struct {
    u8  wave_type;
    u8  loop_flag;
    u16 sample_rate;
    u16 timer_val;
    u16 loop_offset_words;
    u32 nonloop_length_words;
    const u8 *sample_data;
    size_t    sample_bytes;
} BwSwav;

typedef struct {
    const u8 *data;
    size_t    size;
    u32       wave_count;
} BwSwar;

int    bw_swar_open(BwSwar *a, BwBlob blob);
int    bw_swar_wave(const BwSwar *a, u32 index, BwSwav *out);
/* How many PCM16 samples bw_swav_decode() will produce. */
size_t bw_swav_decoded_count(const BwSwav *w);
/* Decode to signed 16-bit PCM regardless of the source encoding; writes at
 * most `cap` samples and returns how many it wrote. */
size_t bw_swav_decode(const BwSwav *w, s16 *out, size_t cap);
/* The loop point as an index into the decoded array (sseqsynth.py's
 * decode_with_loop(): PCM8 4 samples/word, PCM16 2, ADPCM 8 minus the
 * one-word predictor seed), clamped to [0, decoded_count]. */
size_t bw_swav_loop_start(const BwSwav *w, size_t decoded_count);

/* ------------------------------------------------------------------ */
/* SSEQ                                                                */
/* ------------------------------------------------------------------ */

typedef enum {
    BW_OP_UNKNOWN = 0,
    BW_OP_NOTE, BW_OP_REST, BW_OP_PROGRAM_CHANGE, BW_OP_OPEN_TRACK,
    BW_OP_JUMP, BW_OP_CALL, BW_OP_ALLOC_TRACK, BW_OP_RETURN,
    BW_OP_LOOP_END, BW_OP_TRACK_END,
    BW_OP_PAN, BW_OP_VOLUME, BW_OP_TRANSPOSE, BW_OP_EXPRESSION, BW_OP_TEMPO,
    BW_OP_OTHER            /* decoded, but a documented no-op here        */
} BwSseqOp;

typedef struct {
    BwSseqOp op;
    int      note, velocity;
    u32      duration;
    int      value;        /* u8/s8/u16/s16 argument                      */
    u32      offset;       /* OpenTrack / Jump / Call                     */
    int      track_no;
    u32      mask;
    size_t   start, end;
} BwSseqEvent;

typedef struct {
    const u8 *data;
    size_t    size;
    u32       data_offset;
} BwSseq;

int bw_sseq_open(BwSseq *q, BwBlob blob);
/* Decode one event at `pos`.  Returns 0, or -1 for an unrecognised opcode
 * or a read past the end of the buffer (sseq.py's SseqError/IndexError,
 * which run_track distinguishes only by looking at the opcode byte). */
int bw_sseq_decode_event(const u8 *data, size_t size, size_t pos, BwSseqEvent *ev);

/* One note event as the track interpreter produced it. */
typedef struct {
    u32 tick;
    u32 dur;
    int note;              /* already transposed and clamped to 0..127    */
    int velocity;
    int program;
    int volume, expression, pan;
    int track;             /* the track's position in iteration order     */
} BwSeqNoteEvent;

/* Growable arrays, because a track's note count is not known until it has
 * been run.  Both are freed with bw_audio_list_free(). */
typedef struct {
    BwSeqNoteEvent *v;
    size_t n, cap;
} BwSeqNoteList;

typedef struct {
    u32 tick;
    int bpm;
} BwTempoEvent;

typedef struct {
    BwTempoEvent *v;
    size_t n, cap;
} BwTempoList;

/* The track header: track 0 opens with AllocTrack then one OpenTrack per
 * other track naming its start offset.  Returns the number of tracks and
 * fills `starts` in sseqsynth.py's own iteration order, which is the
 * OpenTrack order with track 0 appended last (a Python dict's insertion
 * order, which is observable because it fixes the note list's order and
 * therefore the order of the mix accumulation). */
#define BW_SSEQ_MAX_TRACKS 32
typedef struct {
    int track_no;
    u32 start;
} BwSseqTrackStart;
int bw_sseq_track_starts(const BwSseq *q, BwSseqTrackStart *starts, int cap);

/* Execute one track's bytecode as real control flow: a backward Jump is
 * taken BW_AUDIO_MAX_JUMP_REPEATS times and then ends the track, a forward
 * Jump is taken unconditionally, Call/Return use a real return stack, and
 * the three undocumented opcodes 0xA0/0xA1/0xA2 are skipped by the same
 * assumed 5/2/1 byte widths the oracle uses.  Appends to `notes` and
 * `tempos`; either may be NULL. */
int bw_sseq_run_track(const u8 *data, size_t size, size_t start_pc, int track_index,
                      BwSeqNoteList *notes, BwTempoList *tempos, u32 *end_tick);

/* As bw_sseq_run_track, and sets *looped (when non-NULL) to 1 if the track
 * ended because a backward Jump was refused: the sequence loops on the DS,
 * and the interpreter's one-repeat rule is what cut it here. */
int bw_sseq_run_track_ex(const u8 *data, size_t size, size_t start_pc, int track_index,
                         BwSeqNoteList *notes, BwTempoList *tempos, u32 *end_tick,
                         int *looped);

void bw_seq_note_list_free(BwSeqNoteList *l);
void bw_tempo_list_free(BwTempoList *l);

/* ------------------------------------------------------------------ */
/* Voices: the per-note arithmetic, chunked                            */
/* ------------------------------------------------------------------ */

typedef struct {
    int    active;
    int    psg;                 /* 0 wave, 1 square, 2 noise             */

    /* wave source (borrowed, owned by the render's decode cache) */
    const s16 *samples;
    size_t total_len, loop_start, span;
    int    loop_flag;
    double step;                /* source samples per output sample      */
    double src_pos;

    /* psg source */
    double psg_step;            /* square: cycles per output sample      */
    double phase;
    int    duty;
    int    psg_freq_valid;
    u32    lfsr;                /* noise                                 */
    size_t n_native, native_i;
    double native_last;

    /* envelope and gain */
    double attack_s, decay_s, sustain_level, release_s, note_off_rel;
    double left_gain, right_gain;

    long   start_sample;
    size_t n_out, i;
    double sample_rate;
} BwAudioVoice;

/* Start a voice on a decoded wave.  `samples` is borrowed and must outlive
 * the voice.  Returns 0, or -1 when there is nothing to sound. */
int bw_audio_voice_start_wave(BwAudioVoice *v, const s16 *samples,
                              size_t total_len, size_t loop_start, int loop_flag,
                              double native_rate, const BwRegion *region,
                              int played_note, int pan, double gain,
                              double start_time, double note_off_time,
                              double sample_rate);
/* Start a voice on a PSG channel (region kind square or noise). */
int bw_audio_voice_start_psg(BwAudioVoice *v, const BwRegion *region,
                             int played_note, int pan, double gain,
                             double start_time, double note_off_time,
                             double sample_rate);

/* Mix this voice into a block whose first sample is absolute index
 * `block_start`.  Advances the voice's own state; safe to call with any
 * block size, and chunking is bit-exact against one big call. */
void bw_audio_voice_mix(BwAudioVoice *v, double *left, double *right,
                        size_t n, long block_start);

/* CPython's float.__round__ with no ndigits: round-half-to-even. */
double bw_audio_py_round(double x);

/* The SDK's SND channel-timer formula.  Returns the integer period in
 * timer clocks (>= 1) and, through `actual_hz`, the frequency that period
 * really produces once quantised. */
long   bw_audio_note_timer_ticks(double freq_hz, double *actual_hz);
/* Equal-tempered frequency of a MIDI-style note number, A4 = 69 = 440 Hz. */
double bw_audio_note_freq_hz(int played_note);

/* ------------------------------------------------------------------ */
/* Where a sequence's bytes come from                                  */
/* ------------------------------------------------------------------ */

/*
 * Everything a render needs, as plain byte ranges, so the same sequencer
 * runs on an SDAT slot (the direct path) and on what the game loaded into
 * its own memory and named in an SND command (sdk/arm7/snd_driver.c).
 *
 * `seq` is the range every SSEQ offset (OpenTrack, Jump, Call) is measured
 * from, and `seq_start` is where track 0's first command is, relative to
 * it.  Offsets are relative to the DATA block's payload (file + data_offset,
 * 0x1C in every SSEQ), which is also what the ARM9 passes as seqBase.
 * Measured on this ROM's SDAT: all 2,016 OpenTrack targets in 940
 * sequences follow a track end (0xFF) or a return (0xFD) under that reading
 * (9 under a file-absolute one), and all 9,945 Jump and Call targets land on
 * a command boundary of the payload's linear disassembly (4,710 under the
 * file-absolute one).  Both paths therefore pass the payload: the SND
 * command path seqBase and seqOffset as the ARM9 sent them, and
 * bw_audio_seq_source_sdat() the file's payload with `seq_start` 0.
 *
 * `bank` is the SBNK file image and `wave_arc` the SWAR file image in each
 * of the bank's four slots (size 0: no bank, or an empty slot).
 * `volume` is the SEQ record's volume, 0 meaning unity as in the direct
 * path; `bank_index` is only reported back in BwAudioRender.
 */
typedef struct {
    BwBlob seq;
    u32    seq_start;
    BwBlob bank;
    BwBlob wave_arc[4];
    u8     volume;
    u32    bank_index;
} BwAudioSeqSource;

/* The source for SDAT slot `seq_index`, borrowing the SDAT's bytes.
 * `bank_override` < 0 uses the SEQ record's own bank.  Returns 0, or -1
 * when the SEQ record or its file is missing or not an SSEQ. */
int bw_audio_seq_source_sdat(const BwSdat *s, u32 seq_index, long bank_override,
                             BwAudioSeqSource *out);

/* ------------------------------------------------------------------ */
/* Rendering a whole sequence (the oracle-comparable path)             */
/* ------------------------------------------------------------------ */

/* One prepared note, in the order sseqsynth.py prepares them, with what it
 * resolved for that note.  This is the list tests/test_audio.c dumps and
 * diffs against the oracle's. */
typedef struct {
    BwSeqNoteEvent ev;
    double start_time, off_time;
    int    resolved;            /* a region was found                    */
    BwRegion region;
    double gain;
    int    rendered;            /* it actually reached the mixer         */
} BwAudioNoteInfo;

typedef struct {
    s16   *pcm;                 /* interleaved stereo, owned             */
    size_t n_samples;           /* frames, not bytes                     */
    double duration_seconds;
    double peak_amplitude;
    int    master_bpm;
    u32    bank_index;
    int    track_count;
    int    note_events_total;
    int    notes_rendered;
    int    notes_dropped;
    int    max_concurrent;      /* peak simultaneous voices              */

    BwAudioNoteInfo *notes;     /* prepared notes, owned                 */
    size_t           note_count;
} BwAudioRender;

/* Render SEQ `seq_index` exactly the way sseqsynth.py's render_seq() does,
 * including the peak normalisation and the trailing one-second fade.
 * `bank_override` < 0 uses the SEQ record's own bank field.  Returns 0. */
int  bw_audio_render_seq(const BwSdat *s, u32 seq_index, long bank_override,
                         double max_seconds, BwAudioRender *out);
void bw_audio_render_free(BwAudioRender *r);
/* The same render from a source (bw_audio_render_seq is this over
 * bw_audio_seq_source_sdat). */
int  bw_audio_render_source(const BwAudioSeqSource *src, double max_seconds,
                            BwAudioRender *out);

/* ------------------------------------------------------------------ */
/* The streaming player (the platform layer's audio callback)          */
/* ------------------------------------------------------------------ */

typedef struct BwAudioPlayer BwAudioPlayer;

/* Prepare a sequence for streaming playback.  `max_seconds` bounds the
 * prepared note list the same way the offline render does; `loop` restarts
 * from the top when the last note has finished.  Returns NULL on failure. */
BwAudioPlayer *bw_audio_player_create(const BwSdat *s, u32 seq_index,
                                      double max_seconds, int loop);
void bw_audio_player_free(BwAudioPlayer *p);
/* Fill `frames` stereo frames of signed 16-bit interleaved output.  This is
 * the whole of what an audio callback has to call; it never allocates, never
 * blocks and always writes every frame (silence when nothing is sounding). */
void bw_audio_player_mix(BwAudioPlayer *p, s16 *out, u32 frames);
/* How many note-ons found no free channel out of BW_AUDIO_CHANNELS. */
u32  bw_audio_player_voice_overflows(const BwAudioPlayer *p);
u64  bw_audio_player_frames(const BwAudioPlayer *p);

/* A player over a source (bw_audio_player_create is this over
 * bw_audio_seq_source_sdat).  The source's bytes are borrowed and must
 * outlive the player. */
BwAudioPlayer *bw_audio_player_create_source(const BwAudioSeqSource *src,
                                             double max_seconds, int loop);
/* What bw_audio_player_mix() rounds: `frames` stereo frames of the player's
 * output already scaled by its level, as doubles, written (not added) to
 * `left` and `right`.  bw_audio_player_mix() is exactly this followed by
 * round-half-to-even and the clamp, so a caller that sums several players
 * before rounding gets the same bits as bw_audio_player_mix() when only
 * one is sounding. */
void bw_audio_player_render(BwAudioPlayer *p, double *left, double *right, u32 frames);
/* Every prepared note has started and finished and the end of the
 * sequence has been reached, and the player does not loop. */
int  bw_audio_player_finished(const BwAudioPlayer *p);
/* Whether the sequence loops on the DS (a track ended at a refused
 * backward Jump), and whether this player restarts at its end. */
int  bw_audio_player_seq_loops(const BwAudioPlayer *p);
void bw_audio_player_set_loop(BwAudioPlayer *p, int loop);
/* Sequence ticks (48 to the quarter) played so far, across restarts. */
u64  bw_audio_player_tick(const BwAudioPlayer *p);
/* Bit n set for each track number the sequence opened (track 0 always). */
u32  bw_audio_player_track_mask(const BwAudioPlayer *p);
/* Bit n set while voice n is sounding. */
u32  bw_audio_player_voice_mask(const BwAudioPlayer *p);
/* A gain for the notes track `track_no` (0-15, the SSEQ's own numbering)
 * starts from now on; 1.0 is the default and leaves the arithmetic
 * untouched, 0.0 mutes. */
void bw_audio_player_set_track_gain(BwAudioPlayer *p, int track_no, double gain);

#ifdef __cplusplus
}
#endif

#endif /* EIDOS_ENGINE_AUDIO_H */
