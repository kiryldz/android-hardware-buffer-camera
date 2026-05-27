#!/usr/bin/env python3
"""
Aggregate per-iteration perfetto traces from a Macrobenchmark run into a
results.json that matches the benchmark/baselines/baseline-<gpu>.json schema.

Usage:
    scripts/aggregate-traces.py <traces-dir> <output.json> [options]

    <traces-dir>  Directory containing *.perfetto-trace files (one per iteration).
                  Macrobenchmark writes them to:
                  app/build/outputs/connected_android_test_additional_output/
                    releaseAndroidTest/connected/<device>/
    <output.json> Where to write the aggregated results.

Options:
    --trace-processor PATH   Path to trace_processor binary.
                             Defaults to .cache/frame-latency/trace_processor;
                             auto-downloaded if missing.
    --device-model NAME      Human-readable device name  (e.g. "Pixel 5").
    --gpu NAME               GPU name                     (e.g. "Adreno 620").
    --ftl-model-id ID        FTL model ID                 (e.g. "redfin").
    --android-sdk INT        Android API level             (e.g. 33).
    --duration-s INT         Capture window in seconds    (default 10).
"""

import argparse
import csv
import datetime
import glob
import io
import json
import os
import platform
import stat
import subprocess
import sys
import urllib.request
from collections import defaultdict
from statistics import mean, stdev

SLICE_SQL = (
    "SELECT name, dur FROM slice WHERE name LIKE 'dz.frame_%' AND dur >= 0"
)
COUNTER_SQL = (
    "SELECT c.name, SUM(cs.value) AS total "
    "FROM counter cs "
    "JOIN counter_track c ON cs.track_id = c.id "
    "WHERE c.name LIKE 'dz.dropped_frames.%' "
    "GROUP BY c.name"
)

METRICS = ["avg", "p50", "p90", "p99", "max"]


def download_trace_processor(dest: str) -> None:
    system = platform.system().lower()
    machine = platform.machine().lower()
    if system == "linux":
        url = "https://get.perfetto.dev/trace_processor"
    elif system == "darwin":
        url = "https://get.perfetto.dev/trace_processor"
    else:
        print(f"error: unsupported OS '{system}' for trace_processor auto-download", file=sys.stderr)
        sys.exit(2)
    print(f"Downloading trace_processor -> {dest} ...", file=sys.stderr)
    urllib.request.urlretrieve(url, dest)
    os.chmod(dest, os.stat(dest).st_mode | stat.S_IEXEC)


def run_sql(tp: str, trace: str, sql: str) -> list[dict]:
    result = subprocess.run(
        [tp, "query", trace, sql],
        capture_output=True, text=True, check=True
    )
    rows = []
    reader = csv.DictReader(io.StringIO(result.stdout))
    for row in reader:
        rows.append(row)
    return rows


def percentile(values: list[float], p: float) -> float:
    s = sorted(values)
    k = (len(s) - 1) * p / 100
    lo = int(k)
    hi = min(lo + 1, len(s) - 1)
    return s[lo] + (s[hi] - s[lo]) * (k - lo)


def aggregate_slices(all_durations_ns: list[int]) -> dict:
    if not all_durations_ns:
        return {m: 0.0 for m in ["n"] + METRICS}
    ms = [d / 1e6 for d in all_durations_ns]
    return {
        "n": len(ms),
        "avg": round(mean(ms), 3),
        "p50": round(percentile(ms, 50), 3),
        "p90": round(percentile(ms, 90), 3),
        "p99": round(percentile(ms, 99), 3),
        "max": round(max(ms), 3),
        "stdev": round(stdev(ms) if len(ms) > 1 else 0.0, 3),
    }


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("traces_dir")
    parser.add_argument("output_json")
    parser.add_argument("--trace-processor", default=None)
    parser.add_argument("--device-model", default="unknown")
    parser.add_argument("--gpu", default="unknown")
    parser.add_argument("--ftl-model-id", default="unknown")
    parser.add_argument("--android-sdk", type=int, default=0)
    parser.add_argument("--duration-s", type=int, default=10)
    args = parser.parse_args()

    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    default_tp = os.path.join(root, ".cache", "frame-latency", "trace_processor")
    tp = args.trace_processor or default_tp

    if not os.path.isfile(tp):
        os.makedirs(os.path.dirname(tp), exist_ok=True)
        download_trace_processor(tp)

    traces = sorted(glob.glob(os.path.join(args.traces_dir, "**", "*.perfetto-trace"), recursive=True))
    if not traces:
        # FTL may name them .pftrace
        traces = sorted(glob.glob(os.path.join(args.traces_dir, "**", "*.pftrace"), recursive=True))
    if not traces:
        print(f"error: no .perfetto-trace / .pftrace files found under {args.traces_dir}", file=sys.stderr)
        sys.exit(2)
    print(f"Found {len(traces)} trace(s) under {args.traces_dir}", file=sys.stderr)

    slice_buckets: dict[str, list[int]] = defaultdict(list)
    counter_totals: dict[str, float] = defaultdict(float)

    for trace in traces:
        try:
            for row in run_sql(tp, trace, SLICE_SQL):
                slice_buckets[row["name"]].append(int(row["dur"]))
            for row in run_sql(tp, trace, COUNTER_SQL):
                counter_totals[row["name"]] += float(row["total"])
        except subprocess.CalledProcessError as e:
            print(f"warning: trace_processor failed on {trace}: {e.stderr.strip()}", file=sys.stderr)

    if not slice_buckets:
        print("error: no dz.frame_* slices found in any trace", file=sys.stderr)
        print("  - Ensure the release APK is instrumented and -a com.dz.camerafast was passed to perfetto", file=sys.stderr)
        sys.exit(1)

    stages: dict[str, dict] = {}
    for name in sorted(slice_buckets):
        stages[name] = aggregate_slices(slice_buckets[name])

    counters: dict[str, float] = {k: round(v, 3) for k, v in sorted(counter_totals.items())}

    output = {
        "device_model": args.device_model,
        "gpu": args.gpu,
        "ftl_model_id": args.ftl_model_id,
        "android_sdk": args.android_sdk,
        "captured_at": datetime.datetime.utcnow().isoformat() + "Z",
        "runs": len(traces),
        "duration_s": args.duration_s,
        "stages": stages,
        "counters": counters,
    }

    with open(args.output_json, "w") as f:
        json.dump(output, f, indent=2)
    print(f"Wrote {args.output_json}", file=sys.stderr)

    # Print a summary table to stdout for humans / GHA step logs.
    print(f"\n{'stage':<32} {'n':>5} {'avg':>7} {'p50':>7} {'p90':>7} {'p99':>7} {'max':>7}  (ms)")
    print("-" * 80)
    for name, s in sorted(stages.items()):
        print(f"{name:<32} {s['n']:>5} {s['avg']:>7.2f} {s['p50']:>7.2f} {s['p90']:>7.2f} {s['p99']:>7.2f} {s['max']:>7.2f}")
    if counters:
        print()
        for name, total in sorted(counters.items()):
            print(f"{name:<32} total={total:.0f}")


if __name__ == "__main__":
    main()
