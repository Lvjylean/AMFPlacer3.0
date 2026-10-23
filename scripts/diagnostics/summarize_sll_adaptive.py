#!/usr/bin/env python3
"""Summarize existing SLL policy/timing TSVs; never starts a placement process."""
import argparse
import csv
import json
import math
from collections import defaultdict
from pathlib import Path


def rows(path):
    with path.open() as f:
        return list(csv.DictReader(f, delimiter="\t", quoting=csv.QUOTE_NONE))


def summarize(directory):
    totals = defaultdict(lambda: {"calls": 0, "seconds": 0.0})
    rounds = defaultdict(lambda: defaultdict(float))
    for row in rows(directory / "sll-overhead.tsv"):
        seconds = float(row["wall_seconds"])
        if not math.isfinite(seconds) or seconds < 0:
            raise ValueError("Invalid measured duration")
        category = row["category"]
        totals[category]["calls"] += 1
        totals[category]["seconds"] += seconds
        rounds[(row["phase"], int(row["round"]))][category] += seconds
    decisions = rows(directory / "adaptive-rounds.tsv")
    short = rows(directory / "short-span.tsv")
    return {
        "schema": 1,
        "source": str(directory.resolve()),
        "scope": "Measured diagnostic categories, not an estimate of added total placement runtime",
        "discrete_count_scope": "All reportable nets; includes shared physical-pin span collection when requested",
        "short_span_update_scope": "Per-net hysteresis and activity-mask update using precomputed geometry; no second pin scan",
        "trace_and_log_scope": "Selected-net detail calculation, statistics formatting, output and flush",
        "tau_update_scope": "Scalar decision included in round reporting; no separate all-net pass",
        "category_totals": dict(totals),
        "per_round_seconds": [dict(phase=phase, round=number, categories=dict(values))
                              for (phase, number), values in sorted(rounds.items(), key=lambda item: (item[0][1], item[0][0]))],
        "completed_rounds": decisions,
        "per_qp_exclusion": short,
        "notes": [
            "Discrete S is geometric boundary demand, not measured routed SLL use.",
            "Full S and F retain temporarily inactive nets. QP active-objective differences use a frozen mask.",
            "Do not compare F/J across different tau as if they were the same objective.",
            "qp_solve_window includes existing X/Y solve, guards and thread launch/join; it is not isolated CG CPU time or newly added overhead.",
            "original_writeback_window measures the unchanged writeback path; solver.tsv retains per-axis iteration counts.",
            "Timing-log I/O itself is listed separately in sll-overhead-summary.tsv at normal teardown.",
        ],
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = summarize(args.directory)
    with args.output.open("x") as f:
        json.dump(result, f, indent=2)
        f.write("\n")
    print(json.dumps({"output": str(args.output), "rounds": len(result["completed_rounds"]),
                      "qp_samples": len(result["per_qp_exclusion"])}))


if __name__ == "__main__":
    main()
