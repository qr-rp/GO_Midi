#!/usr/bin/env python3

"""Render note CSV files from transpose_compare to a deterministic WAV preview."""

import argparse
import csv
import math
import wave

import numpy as np


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("input_csv")
    parser.add_argument("output_wav")
    parser.add_argument("--duration", type=float, default=60.0)
    parser.add_argument("--start", type=float, default=0.0)
    parser.add_argument("--sample-rate", type=int, default=22050)
    return parser.parse_args()


def midi_to_hz(pitch: int) -> float:
    return 440.0 * (2.0 ** ((pitch - 69) / 12.0))


def render_note(sample_rate: int, pitch: int, duration: float, velocity: int) -> np.ndarray:
    limited_duration = min(max(duration, 0.03), 8.0)
    sample_count = max(1, int(limited_duration * sample_rate))
    time = np.arange(sample_count, dtype=np.float32) / sample_rate

    attack = np.minimum(time / 0.006, 1.0)
    decay = np.exp(-time * 1.4)
    release_start = max(0.0, limited_duration - 0.04)
    release = np.clip((limited_duration - time) / 0.04, 0.0, 1.0)
    release[time < release_start] = 1.0
    envelope = attack * decay * release

    frequency = midi_to_hz(pitch)
    tone = (
        np.sin(2.0 * math.pi * frequency * time)
        + 0.42 * np.sin(2.0 * math.pi * frequency * 2.0 * time)
        + 0.18 * np.sin(2.0 * math.pi * frequency * 3.0 * time)
        + 0.08 * np.sin(2.0 * math.pi * frequency * 4.0 * time)
    )
    amplitude = max(0, min(velocity, 127)) / 127.0
    return (tone * envelope * amplitude).astype(np.float32)


def main() -> None:
    args = parse_args()
    total_samples = max(1, int(args.duration * args.sample_rate))
    mix = np.zeros(total_samples, dtype=np.float32)
    note_count = 0

    with open(args.input_csv, newline="", encoding="utf-8") as source:
        reader = csv.DictReader(source)
        for row in reader:
            start = float(row["start"])
            end = float(row["end"])
            if end <= args.start or start >= args.start + args.duration:
                continue

            pitch = int(row["pitch"])
            velocity = int(row.get("velocity") or 100)
            duration = max(0.0, end - start)
            tone = render_note(args.sample_rate, pitch, duration, velocity)

            offset = int(round((start - args.start) * args.sample_rate))
            source_start = max(0, -offset)
            target_start = max(0, offset)
            copy_count = min(len(tone) - source_start, total_samples - target_start)
            if copy_count <= 0:
                continue

            mix[target_start:target_start + copy_count] += tone[source_start:source_start + copy_count]
            note_count += 1

    peak = float(np.max(np.abs(mix))) if mix.size else 0.0
    if peak > 0.98:
        mix *= 0.98 / peak
    samples = np.clip(mix * 32767.0, -32768, 32767).astype(np.int16)

    with wave.open(args.output_wav, "wb") as output:
        output.setnchannels(1)
        output.setsampwidth(2)
        output.setframerate(args.sample_rate)
        output.writeframes(samples.tobytes())

    print(f"rendered={args.output_wav} notes={note_count} duration={args.duration:.1f}s")


if __name__ == "__main__":
    main()
