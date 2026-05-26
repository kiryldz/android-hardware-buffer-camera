#!/usr/bin/env bash
#
# Establish a frame-latency baseline: run scripts/measure-frame-latency.sh N
# times (default 5) for D seconds each (default 10), force-stopping the app
# between runs, then aggregate the per-run tables and print run-to-run
# dispersion (mean / stdev / range / CV%) for every metric.
#
# Usage:
#   scripts/baseline-frame-latency.sh             # 5 runs × 10 s
#   scripts/baseline-frame-latency.sh 5 10        # explicit
#
# Output also written to .cache/frame-latency/baseline.{txt,json} so CI (or
# a future PR-vs-main diff step) can ingest the numbers without re-parsing.

set -euo pipefail

RUNS="${1:-5}"
DURATION="${2:-10}"
if ! [[ "$RUNS" =~ ^[1-9][0-9]*$ ]] || ! [[ "$DURATION" =~ ^[1-9][0-9]*$ ]]; then
  echo "usage: $0 [runs] [seconds]   (both positive integers)" >&2
  exit 2
fi

PKG="com.dz.camerafast"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
CACHE_DIR="$ROOT/.cache/frame-latency"
RUNS_DIR="$CACHE_DIR/runs"
mkdir -p "$RUNS_DIR"
rm -f "$RUNS_DIR"/run_*.txt

echo "Running ${RUNS} × ${DURATION}s on attached device, force-stopping ${PKG} between runs."
echo

for i in $(seq 1 "$RUNS"); do
  echo "=========================== RUN $i / $RUNS ==========================="
  "$SCRIPT_DIR/measure-frame-latency.sh" "$DURATION" | tee "$RUNS_DIR/run_$i.txt"
  adb shell am force-stop "$PKG" >/dev/null || true
  echo
  # Brief idle so the next run's warm-up isn't competing with the previous teardown.
  [[ "$i" -lt "$RUNS" ]] && sleep 2
done

BASELINE_TXT="$CACHE_DIR/baseline.txt"
BASELINE_JSON="$CACHE_DIR/baseline.json"

python3 - "$RUNS_DIR" "$RUNS" "$DURATION" "$BASELINE_TXT" "$BASELINE_JSON" <<'PY'
import collections, json, re, statistics, sys, glob, os

runs_dir, runs, duration, out_txt, out_json = sys.argv[1:6]
runs, duration = int(runs), int(duration)

data = collections.defaultdict(list)
stages = []
metrics = ["n", "avg", "p50", "p90", "p99", "max"]
line_re = re.compile(
    r"^(dz\.frame_\S+)\s+(\d+)\s+([\d.]+)\s+([\d.]+)\s+([\d.]+)\s+([\d.]+)\s+([\d.]+)"
)

for path in sorted(glob.glob(os.path.join(runs_dir, "run_*.txt"))):
    with open(path) as f:
        for line in f:
            m = line_re.match(line)
            if not m: continue
            stage = m.group(1)
            if stage not in stages: stages.append(stage)
            data[(stage, "n")].append(int(m.group(2)))
            for k, j in zip(metrics[1:], range(3, 8)):
                data[(stage, k)].append(float(m.group(j)))

# Build text table.
lines = []
hdr = f"{'stage':<28} {'metric':<5} {'mean':>7} {'stdev':>7} {'range':>7} {'CV%':>6}   raw"
lines.append(hdr)
lines.append("-" * len(hdr))
for stage in sorted(stages):
    for metric in metrics[1:]:
        vals = data[(stage, metric)]
        mu = statistics.mean(vals)
        sd = statistics.stdev(vals) if len(vals) > 1 else 0.0
        rg = max(vals) - min(vals)
        cv = (sd / mu * 100) if mu > 0 else 0.0
        raw = " ".join(f"{v:6.2f}" for v in vals)
        lines.append(f"{stage:<28} {metric:<5} {mu:7.2f} {sd:7.2f} {rg:7.2f} {cv:5.1f}%  {raw}")
    lines.append("")
lines.append(f"{'stage':<28} {'n_mean':>7} {'n_stdev':>8} {'n_range':>8}    raw")
lines.append("-" * 70)
for stage in sorted(stages):
    vals = data[(stage, "n")]
    mu = statistics.mean(vals); sd = statistics.stdev(vals) if len(vals) > 1 else 0.0
    raw = " ".join(f"{v:4d}" for v in vals)
    lines.append(f"{stage:<28} {mu:7.1f} {sd:8.2f} {max(vals)-min(vals):8d}    {raw}")

text = "\n".join(lines)
print()
print(text)
with open(out_txt, "w") as f: f.write(text + "\n")

# Machine-readable form for future PR-vs-main diff jobs.
out = {
    "runs": runs,
    "duration_s": duration,
    "stages": {},
}
for stage in sorted(stages):
    out["stages"][stage] = {}
    for metric in metrics:
        vals = data[(stage, metric)]
        mu = statistics.mean(vals)
        sd = statistics.stdev(vals) if len(vals) > 1 else 0.0
        out["stages"][stage][metric] = {
            "mean": round(mu, 3),
            "stdev": round(sd, 3),
            "cv_pct": round((sd / mu * 100) if mu > 0 else 0.0, 2),
            "values": vals,
        }
with open(out_json, "w") as f: json.dump(out, f, indent=2)

print()
print(f"baseline saved: {out_txt}")
print(f"               {out_json}")
PY
