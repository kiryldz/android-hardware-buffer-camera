---
name: frame-latency-baseline
description: Establish a frame-latency baseline by running 5 × 10s Perfetto captures of CameraActivity on the attached device, force-stopping the app between runs, and printing run-to-run dispersion (mean / stdev / CV%) for every dz.frame_* slice. Use to validate that an instrumentation- or rendering-path change hasn't regressed, or to refresh the baseline numbers on a new device.
---

# frame-latency-baseline

Runs the one-shot frame-latency measurement five times and aggregates dispersion across the runs, so that a single regression-vs-noise judgement can be made from the same command.

## Preconditions

- A device is connected via `adb` (exactly one device in `device` state; export `ANDROID_SERIAL=<serial>` if multiple).
- The **release** APK is installed — debug NDK builds add noticeable overhead and would invalidate the baseline. Release is signed with the debug keystore and declares `<profileable android:shell="true"/>`, so:
  ```bash
  ./gradlew :app:installRelease -Pandroid.injected.build.abi=$(adb shell getprop ro.product.cpu.abi | tr -d '\r')
  ```
- `python3` and `curl` are on `PATH`. The first run downloads Perfetto's `trace_processor` into `.cache/frame-latency/` (gitignored, ~25 MB).

## Run it

```bash
scripts/baseline-frame-latency.sh             # 5 runs × 10 s   (defaults)
scripts/baseline-frame-latency.sh 3 5          # 3 runs × 5 s   (faster smoke test)
```

The script:

1. Loops the chosen number of times. Each iteration:
   - Calls `scripts/measure-frame-latency.sh <seconds>`, which `force-stop`s the app, grants `CAMERA`, launches `CameraActivity`, captures a Perfetto trace with `-a com.dz.camerafast`, and prints the per-run table.
   - `adb shell am force-stop com.dz.camerafast` after the capture so the next iteration starts from a cold launch.
   - Sleeps 2 s between iterations.
2. Parses each per-run table, aggregates `n / avg / p50 / p90 / p99 / max` across the runs, and prints the dispersion table.
3. Writes `.cache/frame-latency/baseline.txt` (the table) and `.cache/frame-latency/baseline.json` (machine-readable, one entry per stage × metric with `mean / stdev / cv_pct / values`).

The JSON is the form CI will diff against in a future commit (PR-vs-main regression gate). Treat it as the artifact, the txt as a human view.

## How to read the dispersion table

For each stage × metric you get `mean`, `stdev`, `range`, `CV% = stdev / mean * 100`, plus the raw 5 values.

CV% is the call to make:
- **< 5%** — stable enough to gate PRs on (~5% tolerance).
- **5–10%** — usable with a wider gate or as a secondary signal.
- **> 10%** — too noisy to gate; track trend only.

Findings from the SM-F936B / Android 16 baseline (see CLAUDE.md for the latest numbers):
- `frame_e2e.{gl,vk}.avg`, `frame_e2e.{gl,vk}.p90`, `frame_to_screen.{gl,vk}.p90` are consistently sub-2% CV at 10 s — these are the headline gating metrics.
- `frame_native_proc.{gl,vk}` p90/p99 fluctuate 6–16% — informational only.
- All `max` columns and `p50` on screen-facing stages are bimodal / outlier-driven — don't gate.

## When to re-establish the baseline

- New device or new Android version.
- Renderer-impacting code change merged to `main` (and you want the post-merge numbers as the new floor).
- Capture-tooling change (Perfetto version, atrace categories, capture duration).

Bumping the baseline because "the numbers got worse and CI was failing" is the wrong move — the point of the baseline is to fail in that case. Investigate first.

## Gotchas

- **`-a com.dz.camerafast` is mandatory** in the perfetto invocation. Without it, app-tag atrace sections (which is where `Trace.beginAsyncSection` and `ATrace_beginAsyncSection` slices land) are filtered out and the trace processor will return zero `dz.frame_*` rows. The wrapper script always sets it; if you ever invoke `perfetto` by hand, don't forget.
- **Foldables / multi-display devices** still need an explicit display id only if you screenshot — `screencap` corrupts its PNG output without `-d <id>`. Trace capture is unaffected.
- **N is intentionally hard-coded to 5 runs by default.** Three runs is enough to spot mean drift but not to estimate stdev reliably; five is the cheapest count that gives a meaningful CV. Above 10, you're spending capture minutes for diminishing accuracy.

## Failure modes the script reports

- `error: no adb device in 'device' state.` — plug the device in, accept the ADB prompt.
- `error: multiple adb devices attached` — set `ANDROID_SERIAL=<serial>`.
- `error: com.dz.camerafast is not installed` — run the `installDebug` command from "Preconditions".
- `error: no dz.frame_* slices captured.` — usually means the APK is older than the instrumentation commit; rebuild and reinstall.
