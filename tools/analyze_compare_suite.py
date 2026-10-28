#!/usr/bin/env python3

"""Aggregate audio and note-level fidelity metrics for comparison scenarios."""

import argparse
import csv
from pathlib import Path

import numpy as np

from analyze_audio_compare import (
    chromagram,
    finite_correlation,
    read_wav,
)
from analyze_note_compare import max_polyphony, match_onsets, read_notes


VARIANTS = ("legacy_physical", "improved_physical", "improved_logical")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("comparison_root")
    parser.add_argument("output_csv")
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    root = Path(args.comparison_root)
    rows = []

    for scenario in sorted(path for path in root.iterdir() if path.is_dir()):
        source_csv = scenario / "source_melodic.csv"
        source_wav = scenario / "source_melodic.wav"
        if not source_csv.exists() or not source_wav.exists():
            continue

        source_notes = read_notes(str(source_csv))
        source_audio, sample_rate = read_wav(str(source_wav))
        source_chroma, source_envelope = chromagram(source_audio, sample_rate)
        active = source_envelope > max(float(np.percentile(source_envelope, 20)), 1e-5)

        for variant in VARIANTS:
            variant_notes_path = scenario / f"{variant}_notes.csv"
            variant_audio_path = scenario / f"{variant}.wav"
            if not variant_notes_path.exists() or not variant_audio_path.exists():
                continue

            variant_notes = read_notes(str(variant_notes_path))
            variant_audio, variant_rate = read_wav(str(variant_audio_path))
            sample_count = min(len(source_audio), len(variant_audio))
            if sample_rate != variant_rate:
                raise ValueError(f"sample-rate mismatch in {scenario}")

            variant_chroma, variant_envelope = chromagram(
                variant_audio[:sample_count], sample_rate)
            cosine = np.sum(
                source_chroma[:variant_chroma.shape[0]][active[:variant_chroma.shape[0]]] *
                variant_chroma[active[:variant_chroma.shape[0]]],
                axis=1,
            )
            matched_source, matched_variant = match_onsets(
                source_notes, variant_notes, 0.03)

            rows.append({
                "scenario": scenario.name,
                "variant": variant,
                "chroma_similarity": round(float(cosine.mean()), 6),
                "envelope_correlation": round(finite_correlation(
                    source_envelope[:variant_envelope.size], variant_envelope), 6),
                "onset_recall": round(
                    matched_source / max(len(source_notes), 1), 6),
                "onset_precision": round(
                    matched_variant / max(len(variant_notes), 1), 6),
                "source_notes": len(source_notes),
                "output_notes": len(variant_notes),
                "source_duration": round(
                    sum(note["end"] - note["start"] for note in source_notes), 3),
                "output_duration": round(
                    sum(note["end"] - note["start"] for note in variant_notes), 3),
                "source_polyphony": max_polyphony(source_notes),
                "output_polyphony": max_polyphony(variant_notes),
            })

    output_path = Path(args.output_csv)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    with open(output_path, "w", newline="", encoding="utf-8") as output:
        writer = csv.DictWriter(output, fieldnames=list(rows[0].keys()))
        writer.writeheader()
        writer.writerows(rows)
    print(f"wrote {len(rows)} rows to {output_path}")


if __name__ == "__main__":
    main()
