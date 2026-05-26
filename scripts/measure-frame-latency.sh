#!/usr/bin/env bash
#
# Launch CameraActivity, capture a Perfetto trace for N seconds, and print
# avg/p50/p90/p99/max for every dz.frame_*.{gl,vk} async slice emitted by the
# instrumentation in app/src/main/{java,native/cpp}/.
#
# Usage:
#   scripts/measure-frame-latency.sh [seconds]      (default: 10)
#
# Requires: adb on PATH, python3, curl (only the first time, to fetch
# Perfetto's trace_processor). Device must already have the release APK
# installed; run `./gradlew :app:installRelease -Pandroid.injected.build.abi=$(adb shell getprop ro.product.cpu.abi)`
# first if needed. Release is signed with the debug keystore and declares
# <profileable android:shell="true"/> so Perfetto can attach.

set -euo pipefail

DURATION="${1:-10}"
if ! [[ "$DURATION" =~ ^[1-9][0-9]*$ ]]; then
  echo "error: duration must be a positive integer (seconds); got '$DURATION'" >&2
  exit 2
fi

PKG="com.dz.camerafast"
ACTIVITY="${PKG}/.CameraActivity"

# Cache trace_processor + intermediate artifacts under .cache/ so we don't
# repeatedly download the binary or collide with /tmp on shared machines.
CACHE_DIR="$(cd "$(dirname "$0")/.." && pwd)/.cache/frame-latency"
mkdir -p "$CACHE_DIR"
TP="$CACHE_DIR/trace_processor"
TRACE="$CACHE_DIR/trace.pftrace"
DURATIONS_CSV="$CACHE_DIR/durations.csv"

need() { command -v "$1" >/dev/null 2>&1 || { echo "error: $1 not on PATH" >&2; exit 2; }; }
need adb
need python3

# 1. Preflight: exactly one device
devices="$(adb devices | awk 'NR>1 && $2=="device" {print $1}')"
count="$(echo "$devices" | grep -c . || true)"
if [[ "$count" -eq 0 ]]; then
  echo "error: no adb device in 'device' state. Plug a device in and authorize ADB." >&2
  exit 1
elif [[ "$count" -gt 1 ]]; then
  echo "error: multiple adb devices attached:" >&2
  echo "$devices" >&2
  echo "Pick one with: export ANDROID_SERIAL=<serial>" >&2
  exit 1
fi
serial="$devices"
abi="$(adb shell getprop ro.product.cpu.abi | tr -d '\r')"
sdk="$(adb shell getprop ro.build.version.sdk | tr -d '\r')"
model="$(adb shell getprop ro.product.model | tr -d '\r')"
echo "Device: $model (serial=$serial, sdk=$sdk, abi=$abi)"

# 2. APK installed?
if ! adb shell cmd package list packages -3 | tr -d '\r' | grep -qx "package:${PKG}"; then
  echo "error: $PKG is not installed on the device." >&2
  echo "Install the release variant (debug NDK is slower; release is signed with the debug key):" >&2
  echo "  ./gradlew :app:installRelease -Pandroid.injected.build.abi=$abi" >&2
  exit 1
fi

# 3. Trace processor: download once if missing.
if [[ ! -x "$TP" ]]; then
  need curl
  echo "Downloading trace_processor into $TP ..."
  curl -sSL https://get.perfetto.dev/trace_processor -o "$TP"
  chmod +x "$TP"
fi

# 4. Launch the app fresh (force-stop so prior config doesn't carry over).
adb shell pm grant "$PKG" android.permission.CAMERA
adb shell am force-stop "$PKG"
adb shell am start -n "$ACTIVITY" >/dev/null
# Give camera + GPU contexts a moment to initialize before we start measuring.
sleep 2

# 5. Capture. -a <pkg> is required for app-tag atrace sections to actually land in the trace.
DEVICE_TRACE="/data/misc/perfetto-traces/dz-frame-latency.pftrace"
echo "Capturing ${DURATION}s of Perfetto trace ..."
adb shell "perfetto -o ${DEVICE_TRACE} -t ${DURATION}s -b 32mb -a ${PKG} gfx view app sched" \
  | grep -E "Wrote|Connected" || true
adb pull "$DEVICE_TRACE" "$TRACE" >/dev/null
adb shell rm "$DEVICE_TRACE" 2>/dev/null || true
echo "Pulled trace -> $TRACE ($(du -h "$TRACE" | cut -f1))"

# 6. Extract durations for our slices into a CSV the python step can consume.
"$TP" query "$TRACE" \
  "SELECT name, dur FROM slice WHERE name LIKE 'dz.frame_%' AND dur >= 0" \
  > "$DURATIONS_CSV" 2>/dev/null

rows="$(($(wc -l < "$DURATIONS_CSV") - 1))"
if [[ "$rows" -le 0 ]]; then
  echo "error: no dz.frame_* slices captured. Ruled-out causes:" >&2
  echo "  - APK out of date (rebuild with installDebug)." >&2
  echo "  - perfetto was not invoked with -a $PKG (already handled above)." >&2
  echo "  - Camera permission denied / activity did not actually run." >&2
  exit 1
fi
echo "Captured $rows slices total."
echo

# 7. Aggregate + print.
python3 - "$DURATIONS_CSV" "$DURATION" <<'PY'
import csv, collections, sys

path, duration_s = sys.argv[1], int(sys.argv[2])
buckets = collections.defaultdict(list)
with open(path) as f:
    for row in csv.DictReader(f):
        buckets[row["name"]].append(int(row["dur"]))

def pct(values, p):
    s = sorted(values)
    k = (len(s) - 1) * p / 100
    lo = int(k); hi = min(lo + 1, len(s) - 1)
    return s[lo] + (s[hi] - s[lo]) * (k - lo)

ms = lambda ns: f"{ns/1e6:7.2f}"
hdr = f"{'stage':<28} {'n':>4} {'avg':>7} {'p50':>7} {'p90':>7} {'p99':>7} {'max':>7}  (ms)"
print(hdr)
print("-" * len(hdr))
for name in sorted(buckets):
    v = buckets[name]
    avg = sum(v) / len(v)
    print(f"{name:<28} {len(v):>4} {ms(avg)} {ms(pct(v,50))} {ms(pct(v,90))} {ms(pct(v,99))} {ms(max(v))}")
PY

echo
echo "Re-query the trace at $TRACE with: $TP query $TRACE \"<sql>\""
