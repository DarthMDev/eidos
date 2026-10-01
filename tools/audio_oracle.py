#!/usr/bin/env python3
"""
tools/audio_oracle.py -- generate the audio oracle for engine/audio.

The oracle is aletheia's own asset tool, `tools/assets/sseqsynth.py`: the
sample-based SSEQ renderer that lane wrote from scratch against the ROM's
own SDAT, independently of the C engine.  This script imports it out of the
aletheia checkout (nothing is copied here, and aletheia is never modified),
renders each requested SEQ and writes two files per sequence into the output
directory:

    seqNNNN.wav        the .wav sseqsynth.py itself writes, 32768 Hz stereo
                       signed 16-bit, through that tool's own write_wav()
    seqNNNN.notes.txt  the sequencer's note event list and the render's
                       summary, in the exact line format tests/test_audio.c
                       prints for the C engine, so the two can be diffed

plus `seqs.txt`, a manifest naming every sequence rendered, its SYMB name
and the render window, which is what the C test reads so the set of
sequences is changed in one place (CMakeLists.txt).

The note dump is not a by-product: it is the first half of the pass
criterion.  Two synthesizers can agree on a waveform by accident far less
easily than they can disagree on which notes exist, and a diff of these
lists says *which* note went wrong when the audio does.  Every float is
printed with `%.17g`, which round-trips a binary64 exactly, so a difference
in the dump is a real difference and not a formatting artifact.

The renders are game data derived from the player's own ROM: they are
written into the build directory only, never into this repository.
"""

import argparse
import os
import struct
import sys


def load_oracle(aletheia_root):
    assets = os.path.join(aletheia_root, "tools", "assets")
    if not os.path.isdir(assets):
        raise SystemExit(f"aletheia asset tools not found at {assets}")
    sys.path.insert(0, assets)
    import sdat        # noqa: E402
    import sbnk        # noqa: E402
    import sseqsynth   # noqa: E402
    if not jump_reading_is_payload_relative(sseqsynth):
        follow_jumps_from_payload(sseqsynth)
    return sdat, sbnk, sseqsynth


def _probe_notes(synth):
    """Run the oracle's interpreter on a nine-byte payload behind a header
    of 0xFF bytes: Jump +5, TrackEnd, then a note.  Reading the Jump target
    from the payload reaches the note; reading it as a file offset lands in
    the header's 0xFF and ends the track with no note.  A Call probe is the
    same with Call +5 and the note followed by Return, TrackEnd."""
    head = bytes([0xFF]) * 0x1C
    jump = head + bytes([0x94, 5, 0, 0, 0xFF, 60, 100, 48, 0xFF])
    call = head + bytes([0x95, 5, 0, 0, 0xFF, 60, 100, 48, 0xFD])
    out = []
    for data in (jump, call):
        notes, _tempos, _end = synth.run_track(data, 0x1C, 0x1C)
        out.append(len(notes))
    return out


def jump_reading_is_payload_relative(synth):
    """Whether this sseqsynth.py already reads Jump and Call targets from
    the payload.  aletheia commit 1609c2263 moved OpenTrack to the payload
    reading; its run_track still takes Jump and Call targets as file
    offsets.  Measured on the game's SDAT, all 9,945 Jump and Call targets
    are command boundaries under the payload reading and 4,710 under the
    file one, and 211 of the 864 sequences that decode play different notes,
    SEQ_BGM_GYM among them (1,322 note events against 2,510)."""
    try:
        return _probe_notes(synth) == [1, 1]
    except TypeError:
        return False


def follow_jumps_from_payload(synth):
    """Make the imported oracle add data_offset to Jump and Call targets,
    the one-line change its run_track still lacks, and nothing else.  The
    decoder is wrapped for the duration of each run_track call only.  The
    probe above turns this off by itself once aletheia carries the fix."""
    import sseq as sseqmod
    orig_run = synth.run_track
    orig_decode = sseqmod.decode_event

    def run_track(data, start_pc, data_offset):
        def decode(d, pc):
            ev, new_pc = orig_decode(d, pc)
            if ev.get("op") in ("Jump", "Call"):
                ev = dict(ev)
                ev["offset"] = ev["offset"] + data_offset
            return ev, new_pc
        sseqmod.decode_event = decode
        try:
            return orig_run(data, start_pc, data_offset)
        finally:
            sseqmod.decode_event = orig_decode

    synth.run_track = run_track
    if _probe_notes(synth) != [1, 1]:
        raise SystemExit("audio_oracle: could not correct the oracle's Jump/Call reading")
    print("[*] oracle: Jump/Call targets read from the payload (aletheia's "
          "run_track still reads them as file offsets)")


def g17(x):
    return "%.17g" % x


def bits(x):
    """A double's exact IEEE-754 bit pattern, as 16 lowercase hex digits.

    The note dump is diffed line for line against what the C engine prints,
    so a float in it has to be written the same way by CPython and by the
    host's printf.  A decimal rendering is not safe for that: the two
    libraries need not round the 17th significant digit the same way.  The
    bit pattern has no such freedom, and it is also the only rendering in
    which "these two doubles are the same double" is exactly what the diff
    says."""
    return "%016x" % struct.unpack("<Q", struct.pack("<d", float(x)))[0]


def region_kind(region):
    if region is None:
        return "none"
    kind = region.get("kind")
    if kind == "psg_square":
        return "square"
    if kind == "psg_noise":
        return "noise"
    return "wave"


def dump_notes(path, synth, sbnkmod, s, seq_index, stats, n_frames):
    """Re-walk the same sequence sseqsynth.render_seq() just rendered and
    print one line per note event, in the order render_seq prepares them
    (track iteration order, then note order within a track).  Every value
    printed is read back out of the oracle's own functions, not
    recomputed by this script."""
    rec = s.seq_record(seq_index)
    brec = s.bank_record(rec["bank"])
    bnk = None
    if brec is not None:
        bnk = sbnkmod.Sbnk(s.raw_file(brec["file_id"]))
        bnk.parse_all_details()

    import sseq as sseqmod
    seq_obj = sseqmod.Sseq(s.raw_file(rec["file_id"]))
    tracks = synth.get_tracks(seq_obj)

    all_tempo_events = []
    for t in tracks.values():
        all_tempo_events.extend(t["tempo_events"])
    tempo_map = synth.build_tempo_map(all_tempo_events, synth.DEFAULT_BPM)
    seq_volume_gain = rec["volume"] / 127.0 if rec["volume"] else 1.0

    lines = []
    total = 0
    for track_index, t in enumerate(tracks.values()):
        for n in t["notes"]:
            start_time = synth.tick_to_seconds(tempo_map, n["tick"])
            off_time = synth.tick_to_seconds(tempo_map, n["tick"] + n["dur"])
            region = synth.resolve_instrument_region(bnk, n["program"], n["note"])
            kind = region_kind(region)
            if region is None:
                gain = 0.0
                base = a = d = su = r = rpan = duty = 0
                warc = wid = -1
            else:
                gain = seq_volume_gain * (n["volume"] / 127.0) \
                    * (n["expression"] / 127.0) * (n["vel"] / 127.0)
                base = region["base_note"]
                a, d = region["attack"], region["decay"]
                su, r = region["sustain"], region["release"]
                rpan = region["pan"]
                duty = region.get("duty", 0) if kind == "square" else 0
                if kind == "wave":
                    warc, wid = region["wave_arc"], region["wave_id"]
                else:
                    warc, wid = -1, -1
            lines.append(
                "%d t%d tick=%d dur=%d note=%d vel=%d prog=%d vol=%d exp=%d "
                "pan=%d start=%s off=%s region=%s base=%d a=%d d=%d s=%d r=%d "
                "rpan=%d duty=%d warc=%d wid=%d gain=%s"
                % (total, track_index, n["tick"], n["dur"], n["note"], n["vel"],
                   n["program"], n["volume"], n["expression"], n["pan"],
                   bits(start_time), bits(off_time), kind, base, a, d, su, r,
                   rpan, duty, warc, wid, bits(gain)))
            total += 1

    with open(path, "w", encoding="ascii", newline="\n") as fh:
        fh.write("seq=%d bank=%d tracks=%d events=%d bpm=%d\n"
                 % (seq_index, stats["bank_index"], stats["track_count"],
                    total, stats["master_bpm"]))
        for line in lines:
            fh.write(line + "\n")
        fh.write("duration=%s samples=%d rendered=%d dropped=%d\n"
                 % (bits(stats["duration_seconds"]), n_frames,
                    stats["notes_rendered"], stats["notes_dropped"]))
    return total


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--aletheia-root", required=True)
    ap.add_argument("--data-root", required=True,
                    help="the extracted NitroFS tree (the one with wb_sound_data.sdat)")
    ap.add_argument("--sdat", default="wb_sound_data.sdat")
    ap.add_argument("--out", required=True, help="output directory")
    ap.add_argument("--max-seconds", type=float, default=4.0)
    ap.add_argument("seq", type=int, nargs="+")
    args = ap.parse_args(argv)

    sdatmod, sbnkmod, synth = load_oracle(args.aletheia_root)
    path = os.path.join(args.data_root, args.sdat)
    s = sdatmod.parse_file(path)
    os.makedirs(args.out, exist_ok=True)

    manifest = []
    for seq_index in args.seq:
        pcm, stats = synth.render_seq(s, seq_index, max_seconds=args.max_seconds)
        wav_path = os.path.join(args.out, "seq%04d.wav" % seq_index)
        notes_path = os.path.join(args.out, "seq%04d.notes.txt" % seq_index)
        synth.write_wav(wav_path, pcm)
        total = dump_notes(notes_path, synth, sbnkmod, s, seq_index, stats,
                           len(pcm) // 4)
        name = None
        names = s.names_for("SEQ")
        if seq_index < len(names):
            name = names[seq_index]
        manifest.append((seq_index, name or "-", stats))
        print("[*] SEQ %d (%s): %d note events, %d rendered, %d dropped, "
              "%.3fs, peak %.0f -> %s"
              % (seq_index, name or "-", total, stats["notes_rendered"],
                 stats["notes_dropped"], stats["duration_seconds"],
                 stats["peak_amplitude"], os.path.basename(wav_path)))

    with open(os.path.join(args.out, "seqs.txt"), "w",
              encoding="ascii", newline="\n") as fh:
        fh.write("# seq_index name max_seconds\n")
        for seq_index, name, _stats in manifest:
            fh.write("%d %s %s\n" % (seq_index, name, g17(args.max_seconds)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
