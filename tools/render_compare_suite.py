#!/usr/bin/env python3

"""Render every transpose_compare scenario under a directory."""

import argparse
import subprocess
import sys
from pathlib import Path


VARIANTS = (
    "source_melodic",
    "legacy_physical_notes",
    "improved_physical_notes",
    "improved_logical_notes",
)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("comparison_root")
    parser.add_argument("--duration", type=float, default=30.0)
    parser.add_argument("--sample-rate", type=int, default=22050)
    parser.add_argument("--midi-root", type=Path)
    return parser.parse_args()


def main() -> None:
    args = parse_args()
    root = Path(args.comparison_root)
    renderer = Path(__file__).with_name("render_plan_audio.py")
    scenarios = sorted(path for path in root.iterdir() if path.is_dir())

    rendered = 0
    for scenario in scenarios:
        if args.midi_root:
            midi_name = scenario.name.rsplit("_", 1)[0] + ".mid"
            midi_path = args.midi_root / midi_name
            if midi_path.exists():
                output = scenario / "source_original_midi.wav"
                subprocess.run(
                    [
                        "ffmpeg",
                        "-hide_banner",
                        "-loglevel",
                        "warning",
                        "-i",
                        str(midi_path),
                        "-t",
                        str(args.duration),
                        "-y",
                        str(output),
                    ],
                    check=True,
                )
                rendered += 1

        for variant in VARIANTS:
            source = scenario / f"{variant}.csv"
            if not source.exists():
                continue
            output_name = variant.removesuffix("_notes") + ".wav"
            output = scenario / output_name
            subprocess.run(
                [
                    sys.executable,
                    str(renderer),
                    str(source),
                    str(output),
                    "--duration",
                    str(args.duration),
                    "--sample-rate",
                    str(args.sample_rate),
                ],
                check=True,
            )
            rendered += 1
    print(f"rendered {rendered} WAV files under {root}")


if __name__ == "__main__":
    main()
