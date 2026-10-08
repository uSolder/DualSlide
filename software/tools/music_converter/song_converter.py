#!/usr/bin/env python3
"""
song_converter.py

Convert a MIDI file exported from FL Studio into a DualSlide song.

Output:
    inc/<name>_song.h    declares   extern const Music_SongTypeDef <Name>Song;
    src/<name>_song.c    contains   the note list and instrument table

Usage:
    1. In FL Studio: File > Export > MIDI file. Each channel becomes a MIDI
       track named after the channel.
    2. List the tracks:
           python song_converter.py pong.mid --list
    3. Map every track that has notes to an instrument and write the song into
       the game's folder:
           python song_converter.py pong.mid --name pong --loop ^
               --instrument Piano=piano --instrument Bass=bass ^
               --game-dir ../../apps/games/pong

    --instrument TRACK=NAME   TRACK is a track name or index from --list; NAME
                              is an instrument made by instrument_converter.py
                              (assets/instruments/NAME.h). Repeat per track.
    --loop                    Repeat the song. Its length is rounded up to a
                              whole bar so the loop stays in time.
    --ignore-unmapped         Skip tracks with notes but no --instrument.

The song uses the file's first tempo; tempo changes are reported and ignored.
"""

from __future__ import annotations

import argparse
import re
import struct
import sys
from dataclasses import dataclass, field
from pathlib import Path

DEFAULT_TEMPO_US = 500000


@dataclass
class Note:
    start: int
    duration: int
    track: int
    note: int
    velocity: int


@dataclass
class Track:
    index: int
    name: str = ""
    notes: list[Note] = field(default_factory=list)


@dataclass
class Midi:
    ticks_per_quarter: int
    tracks: list[Track]
    tempos: list[tuple[int, int]]
    time_signature: tuple[int, int] | None


def read_variable_length(data: bytes, offset: int) -> tuple[int, int]:
    value = 0
    while True:
        byte = data[offset]
        offset += 1
        value = (value << 7) | (byte & 0x7F)
        if not byte & 0x80:
            return value, offset


def parse_track(data: bytes, index: int, tempos: list, signatures: list) -> Track:
    track = Track(index)
    open_notes: dict[tuple[int, int], list[tuple[int, int]]] = {}
    offset = 0
    tick = 0
    status = 0

    def finish(channel: int, note: int, end: int) -> None:
        starts = open_notes.get((channel, note))
        if starts:
            start, velocity = starts.pop(0)
            track.notes.append(Note(start, max(1, end - start), index, note, velocity))

    while offset < len(data):
        delta, offset = read_variable_length(data, offset)
        tick += delta
        if data[offset] & 0x80:
            status = data[offset]
            offset += 1
        kind = status & 0xF0

        if status == 0xFF:
            meta = data[offset]
            length, offset = read_variable_length(data, offset + 1)
            payload = data[offset:offset + length]
            offset += length
            if meta == 0x03 and not track.name:
                track.name = payload.decode("latin-1").strip()
            elif meta == 0x51 and length == 3:
                tempos.append((tick, int.from_bytes(payload, "big")))
            elif meta == 0x58 and length >= 2:
                signatures.append((tick, payload[0], 2 ** payload[1]))
            elif meta == 0x2F:
                break
        elif status in (0xF0, 0xF7):
            length, offset = read_variable_length(data, offset)
            offset += length
        elif kind in (0x80, 0x90):
            note, velocity = data[offset], data[offset + 1]
            offset += 2
            channel = status & 0x0F
            if kind == 0x90 and velocity > 0:
                open_notes.setdefault((channel, note), []).append((tick, velocity))
            else:
                finish(channel, note, tick)
        elif kind in (0xA0, 0xB0, 0xE0):
            offset += 2
        elif kind in (0xC0, 0xD0):
            offset += 1
        else:
            raise ValueError(f"unexpected MIDI status 0x{status:02X} in track {index}")

    for (channel, note), starts in list(open_notes.items()):
        while starts:
            finish(channel, note, tick)
    return track


def parse_midi(path: Path) -> Midi:
    data = path.read_bytes()
    if data[:4] != b"MThd":
        raise ValueError("not a MIDI file")
    header_length = struct.unpack(">I", data[4:8])[0]
    _, track_count, division = struct.unpack(">HHH", data[8:14])
    if division & 0x8000:
        raise ValueError("SMPTE time division is not supported; export with musical time")

    tracks, tempos, signatures = [], [], []
    offset = 8 + header_length
    for index in range(track_count):
        if data[offset:offset + 4] != b"MTrk":
            raise ValueError(f"track {index} is missing its MTrk header")
        length = struct.unpack(">I", data[offset + 4:offset + 8])[0]
        tracks.append(parse_track(data[offset + 8:offset + 8 + length], index, tempos, signatures))
        offset += 8 + length

    signatures.sort()
    signature = (signatures[0][1], signatures[0][2]) if signatures else None
    return Midi(division, tracks, sorted(tempos), signature)


def pascal_case(name: str) -> str:
    return "".join(part[:1].upper() + part[1:] for part in re.split(r"[^A-Za-z0-9]+", name) if part)


def list_tracks(midi: Midi) -> None:
    print(f"ticks per quarter note: {midi.ticks_per_quarter}")
    for track in midi.tracks:
        if track.notes:
            low = min(n.note for n in track.notes)
            high = max(n.note for n in track.notes)
            print(f"  track {track.index}: '{track.name}'  {len(track.notes)} notes, range {low}-{high}")
        else:
            print(f"  track {track.index}: '{track.name}'  (no notes)")


def resolve_mapping(midi: Midi, mappings: list[str], ignore_unmapped: bool) -> dict[int, str]:
    by_track: dict[int, str] = {}
    for mapping in mappings:
        key, _, instrument = mapping.partition("=")
        if not instrument:
            sys.exit(f"--instrument '{mapping}' must look like TRACK=NAME")
        matches = [t for t in midi.tracks if (key.isdigit() and t.index == int(key)) or t.name == key]
        if not matches:
            sys.exit(f"no track named or numbered '{key}'; run with --list to see the tracks")
        for track in matches:
            by_track[track.index] = re.sub(r"[^A-Za-z0-9_]", "_", instrument).lower()

    unmapped = [t for t in midi.tracks if t.notes and t.index not in by_track]
    if unmapped and not ignore_unmapped:
        names = ", ".join(f"{t.index} '{t.name}'" for t in unmapped)
        sys.exit(f"tracks with notes but no --instrument: {names}\n(map them, or pass --ignore-unmapped)")
    return by_track


def write_song(midi: Midi, mapping: dict[int, str], name: str, loop: bool, inc_dir: Path, src_dir: Path) -> None:
    instruments: list[str] = []
    notes = []
    for track in midi.tracks:
        if track.index not in mapping:
            continue
        instrument = mapping[track.index]
        if instrument not in instruments:
            instruments.append(instrument)
        for note in track.notes:
            notes.append((note.start, instruments.index(instrument), note.note, note.duration, min(127, note.velocity)))
    notes.sort()
    if not notes:
        sys.exit("no notes to convert")
    if len(instruments) > 255:
        sys.exit("a song can use at most 255 instruments")

    tempo = midi.tempos[0][1] if midi.tempos else DEFAULT_TEMPO_US
    if len({t for _, t in midi.tempos}) > 1:
        print(f"warning: tempo changes ignored; using {60000000 / tempo:.2f} BPM throughout")
    numerator, denominator = midi.time_signature or (4, 4)
    bar = numerator * midi.ticks_per_quarter * 4 // denominator
    end = max(start + duration for start, _, _, duration, _ in notes)
    length = -(-end // bar) * bar if loop else end

    symbol = pascal_case(name) + "Song"
    guard = re.sub(r"[^A-Za-z0-9]", "_", name).upper() + "_SONG_H"
    header = (
        "/**\n"
        f" * @file {name}_song.h\n"
        f" * @brief {pascal_case(name)} music.\n"
        " *\n"
        " * Generated by tools/music_converter/song_converter.py; do not edit.\n"
        " */\n\n"
        f"#ifndef {guard}\n#define {guard}\n\n"
        '#include "music.h"\n\n'
        "#ifdef __cplusplus\nextern \"C\" {\n#endif\n\n"
        f"extern const Music_SongTypeDef {symbol};\n\n"
        "#ifdef __cplusplus\n}\n#endif\n\n"
        f"#endif /* {guard} */\n"
    )

    includes = "".join(f'#include "{instrument}.h"\n' for instrument in instruments)
    table = ",\n".join(f"    &Instrument_{pascal_case(i)}" for i in instruments)
    rows = ",\n".join(
        f"    {{ .StartTick = {start}U, .DurationTicks = {duration}U, .Instrument = {instrument}U, .Note = {note}U, .Velocity = {velocity}U }}"
        for start, instrument, note, duration, velocity in notes
    )
    source = (
        "/**\n"
        f" * @file {name}_song.c\n"
        f" * @brief {pascal_case(name)} music data.\n"
        " *\n"
        " * Generated by tools/music_converter/song_converter.py; do not edit.\n"
        " */\n\n"
        f'#include "{name}_song.h"\n\n'
        f"{includes}\n"
        f"static const Music_InstrumentTypeDef *const {symbol}Instruments[] =\n{{\n{table}\n}};\n\n"
        f"static const Music_NoteTypeDef {symbol}Notes[] =\n{{\n{rows}\n}};\n\n"
        f"const Music_SongTypeDef {symbol} =\n{{\n"
        f"    .Instruments = {symbol}Instruments,\n"
        f"    .InstrumentCount = {len(instruments)}U,\n"
        f"    .Notes = {symbol}Notes,\n"
        f"    .NoteCount = {len(notes)}U,\n"
        f"    .TicksPerQuarterNote = {midi.ticks_per_quarter}U,\n"
        f"    .MicrosecondsPerQuarterNote = {tempo}U,\n"
        f"    .LengthTicks = {length}U,\n"
        f"    .Loop = {'true' if loop else 'false'}\n"
        "};\n"
    )

    inc_dir.mkdir(parents=True, exist_ok=True)
    src_dir.mkdir(parents=True, exist_ok=True)
    (inc_dir / f"{name}_song.h").write_text(header, encoding="utf-8", newline="\r\n")
    (src_dir / f"{name}_song.c").write_text(source, encoding="utf-8", newline="\r\n")

    seconds = length * tempo / midi.ticks_per_quarter / 1e6
    print(f"{symbol}: {len(notes)} notes, {len(instruments)} instruments ({', '.join(instruments)}), "
          f"{60000000 / tempo:.2f} BPM, {seconds:.1f} s{' looping' if loop else ''}, ~{len(notes) * 12} bytes of note data")
    print(f"wrote {inc_dir / (name + '_song.h')} and {src_dir / (name + '_song.c')}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("midi", type=Path, help="MIDI file exported from FL Studio")
    parser.add_argument("--list", action="store_true", help="list the tracks and exit")
    parser.add_argument("--name", help="song name, normally the game name (default: MIDI file name)")
    parser.add_argument("--instrument", action="append", default=[], metavar="TRACK=NAME", help="map a track to an instrument")
    parser.add_argument("--loop", action="store_true", help="repeat the song")
    parser.add_argument("--ignore-unmapped", action="store_true", help="skip tracks without an instrument")
    parser.add_argument("--game-dir", type=Path, help="game folder; writes inc/ and src/ files there")
    parser.add_argument("--output", type=Path, default=Path("."), help="output folder when --game-dir is not given")
    args = parser.parse_args()

    try:
        midi = parse_midi(args.midi)
    except (ValueError, IndexError, struct.error) as exc:
        sys.exit(f"could not read {args.midi}: {exc}")

    if args.list:
        list_tracks(midi)
        return

    name = re.sub(r"[^A-Za-z0-9_]", "_", args.name or args.midi.stem).lower()
    mapping = resolve_mapping(midi, args.instrument, args.ignore_unmapped)
    inc_dir = args.game_dir / "inc" if args.game_dir else args.output
    src_dir = args.game_dir / "src" if args.game_dir else args.output
    write_song(midi, mapping, name, args.loop, inc_dir, src_dir)


if __name__ == "__main__":
    main()
