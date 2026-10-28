#!/usr/bin/env python3

"""Aggregate transpose_compare CSV output into one report table."""

import argparse
import csv
import json
from pathlib import Path


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
        summary_path = scenario / "summary.csv"
        metadata_path = scenario / "metadata.txt"
        if not summary_path.exists():
            continue

        with open(summary_path, newline="", encoding="utf-8") as source:
            summary = list(csv.DictReader(source))
        metadata = {}
        if metadata_path.exists():
            for line in metadata_path.read_text(encoding="utf-8").splitlines():
                key, _, value = line.partition("=")
                metadata[key] = value

        for item in summary:
            candidates_path = scenario / f"{item['variant']}_candidates.csv"
            pareto_shifts = []
            selected_pareto = ""
            if candidates_path.exists():
                with open(candidates_path, newline="", encoding="utf-8") as source:
                    candidates = list(csv.DictReader(source))
                pareto_shifts = [
                    int(row["shift"])
                    for row in candidates
                    if row.get("pareto_optimal") == "1"
                ]
                selected_pareto = any(
                    row["shift"] == item["global_shift"] and
                    row.get("pareto_optimal") == "1"
                    for row in candidates
                )
            rows.append({
                "scenario": scenario.name,
                "variant": item["variant"],
                "global_shift": item["global_shift"],
                "retained_percent": item["retained_percent"],
                "folded_notes": item["folded"],
                "conflict_merges": item["conflict_merges"],
                "exact_duplicates": item["exact_duplicates"],
                "source_notes": item["source_notes"],
                "output_notes": item["output_notes"],
                "pareto_shifts": json.dumps(pareto_shifts),
                "selected_pareto": selected_pareto,
                "keyboard_size": metadata.get("keyboard_size", ""),
                "duration": metadata.get("duration", ""),
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
