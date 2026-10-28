#!/usr/bin/env python3

"""Compare source and transformed note CSVs at onset and pitch-class level."""

import argparse
import bisect
import csv
import json


def read_notes(path: str) -> list[dict]:
    notes = []
    with open(path, newline="", encoding="utf-8") as source:
        for row in csv.DictReader(source):
            pitch = int(row["pitch"])
            start = float(row["start"])
            end = float(row["end"])
            if end > start:
                notes.append({
                    "start": start,
                    "end": end,
                    "pitch": pitch,
                    "pitch_class": pitch % 12,
                })
    notes.sort(key=lambda note: note["start"])
    return notes


def max_polyphony(notes: list[dict]) -> int:
    events = []
    for note in notes:
        events.append((note["start"], 1))
        events.append((note["end"], -1))
    events.sort(key=lambda event: (event[0], event[1]))
    active = 0
    maximum = 0
    for _, delta in events:
        active += delta
        maximum = max(maximum, active)
    return maximum


def match_onsets(source: list[dict], transformed: list[dict], tolerance: float) -> tuple[int, int]:
    by_pitch_class = {pitch_class: [] for pitch_class in range(12)}
    for note in transformed:
        by_pitch_class[note["pitch_class"]].append(note["start"])
    for times in by_pitch_class.values():
        times.sort()

    matched_source = 0
    for note in source:
        times = by_pitch_class[note["pitch_class"]]
        index = bisect.bisect_left(times, note["start"] - tolerance)
        if index < len(times) and abs(times[index] - note["start"]) <= tolerance:
            matched_source += 1

    source_times = [(note["pitch_class"], note["start"]) for note in source]
    source_by_pitch = {pitch_class: [] for pitch_class in range(12)}
    for pitch_class, start in source_times:
        source_by_pitch[pitch_class].append(start)
    for times in source_by_pitch.values():
        times.sort()

    matched_transformed = 0
    for note in transformed:
        times = source_by_pitch[note["pitch_class"]]
        index = bisect.bisect_left(times, note["start"] - tolerance)
        if index < len(times) and abs(times[index] - note["start"]) <= tolerance:
            matched_transformed += 1
    return matched_source, matched_transformed


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("source_csv")
    parser.add_argument("transformed_csv")
    parser.add_argument("--tolerance", type=float, default=0.03)
    args = parser.parse_args()

    source = read_notes(args.source_csv)
    transformed = read_notes(args.transformed_csv)
    matched_source, matched_transformed = match_onsets(
        source, transformed, args.tolerance)

    result = {
        "source_notes": len(source),
        "transformed_notes": len(transformed),
        "source_onset_recall": round(matched_source / max(len(source), 1), 6),
        "transformed_onset_precision": round(
            matched_transformed / max(len(transformed), 1), 6),
        "source_duration_sum": round(sum(note["end"] - note["start"] for note in source), 6),
        "transformed_duration_sum": round(
            sum(note["end"] - note["start"] for note in transformed), 6),
        "source_max_polyphony": max_polyphony(source),
        "transformed_max_polyphony": max_polyphony(transformed),
    }
    print(json.dumps(result, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    main()
