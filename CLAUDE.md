# CLAUDE.md — Camera Fast

Project-wide notes for future sessions. Captures non-obvious context — architecture intent, instrumentation contracts, build gotchas, baseline findings. Read this before diving into code, especially before touching the camera→GPU pipeline or the build.

## Code style

Default to no comments. Only add one when the *why* isn't obvious from the code — a non-trivial invariant, a thread/race subtlety, a cross-file contract, or a workaround. Identifiers carry the *what*; long block-comments explaining the *what* are noise. If you find yourself writing one, ask whether the explanation belongs in this file (architecture / design intent) or a skill (workflow steps) instead.

## What this is

Android demo app showing how to feed `AHardwareBuffer`s from the camera into both an **OpenGL ES 3** and a **Vulkan 1.3** renderer with zero CPU copies (when the buffer's `USAGE_GPU_SAMPLED_IMAGE` flag is set), then present to a `TextureView`. Almost all rendering lives in C++ via the NDK; Kotlin is glue + Compose UI.

Single activity (`CameraActivity`). Default view is dual-pane — OpenGL preview on top, Vulkan preview on bottom — clickable to collapse one side. Edge-to-edge UI: previews go behind transparent system bars; the header label and bottom button row respect insets.

## Architecture / data flow

```
CameraX (RGBA_8888) or Camera2 (PRIVATE)
   ↓  (analyzer / OnImageAvailable, on a single worker thread)
imageProxy.image.hardwareBuffer  +  FrameTrace.nextFrameId()
   ↓  CoreEngine.sendCameraFrame()        ← one call per engine (GL + VK)
nativeSendCameraFrame  (JNI)
   ↓
CoreEngine::nativeSendCameraFrame  (C++, worker thread)
   ↓  GPU-sampled path:        renderer->processCameraFrame(buf, …, frameId)
   ↓  CPU fallback path:       memcpy into a self-allocated GPU buffer, then same
BaseRenderer::processCameraFrame
   ↓  schedules a task on the renderer's LooperThread
[render thread]
   ↓  hwBufferToTexture()      ← becomes EGLImage / VkImage backed by external memory
   ↓  postChoreographerCallback()
[Choreographer doFrame on render thread]
   ↓  renderImpl()             ← draws + eglSwapBuffers / vkQueuePresentKHR
```

Key files:
- `app/src/main/java/com/dz/camerafast/CameraX.kt`, `Camera2.kt` — analyzer entry points.
- `app/src/main/java/com/dz/camerafast/CoreEngine.kt` — Kotlin-side JNI surface + `TextureView.SurfaceTextureListener`.
- `app/src/main/native/cpp/core_engine.{hpp,cpp}` — JNI peer; holds the renderer and the `ANativeWindow`.
- `app/src/main/native/cpp/base_renderer.{hpp,cpp}` — render-thread Looper, frame-task scheduling, viewport/MVP coalescing, the `pendingPresentFrameId` interlock.
- `app/src/main/native/cpp/{opengl,vulkan}_renderer.{hpp,cpp}` — backend-specific code; the only place EGL/GL or Vulkan symbols are touched.

The CPU fallback in `nativeSendCameraFrame` exists because **CameraX**'s `ImageAnalysis` delivers HW buffers with `USAGE_CPU_*` flags rather than `USAGE_GPU_SAMPLED_IMAGE`. We re-allocate a GPU buffer once, `memcpy` into it each frame (drops the engine mutex during the slow memcpy so main-thread JNI calls aren't blocked), then hand it to the renderer. Camera2 with `ImageFormat.PRIVATE` + `USAGE_GPU_SAMPLED_IMAGE` skips that copy entirely — that's the point of supporting both APIs.

## Frame-timing instrumentation contract

Per camera frame, the analyzer assigns a process-wide monotonic `frameId: Int` via `FrameTrace.nextFrameId()`. That id is the **cookie** of four ATrace **async** sections per renderer, plus one **sync** sub-section nested inside the renderer's `renderImpl`:

| Section | Kind | Begin (where) | End (where) | What it measures |
|---|---|---|---|---|
| `dz.frame_to_native.<gl\|vk>` | async | Kotlin analyzer | `BaseRenderer::processCameraFrame` JNI entry | Kotlin glue overhead |
| `dz.frame_native_proc.<gl\|vk>` | async | `processCameraFrame` entry | end of `hwBufferToTexture` on render thread | Native processing |
| `dz.frame_to_screen.<gl\|vk>` | async | end of `hwBufferToTexture` | after `eglSwapBuffers` / `vkQueuePresentKHR` | Submit→present (mostly vsync wait at 60 Hz vs 30 fps camera) |
| `dz.frame_render.<gl\|vk>` | sync | `renderImpl` entry | just before `eglSwapBuffers` / `vkQueuePresentKHR` | Actual GPU command submission only — subtract from `frame_to_screen` to isolate vsync wait |
| `dz.frame_e2e.<gl\|vk>` | async | Kotlin analyzer (same point as `to_native` begin) | swap/present returned | **End-to-end** |

A counter track tracks dropped frames per renderer:

| Counter | When incremented |
|---|---|
| `dz.dropped_frames.<gl\|vk>` | When a newer camera frame's texture is staged before the previous one's Choreographer callback fires |

Section + counter name constants live in **one** place — `app/src/main/native/cpp/frame_trace.hpp`. The Kotlin `FrameTrace` object exposes `@JvmStatic external` methods backed by `app/src/main/native/cpp/frame_trace.cpp`; its `FRAME_E2E_GL` / `FRAME_E2E_VK` / `FRAME_TO_NATIVE_GL` / `FRAME_TO_NATIVE_VK` `@JvmField val`s are populated from C++ at class-load. Don't add new constants to Kotlin — extend `traceNames` in `frame_trace.hpp` and add a getter.

Design decisions worth remembering:
- **`frameId` is an `Int`, not the sensor timestamp.** ATrace's async-section cookie is 32-bit; sensor timestamps in nanoseconds overflow. A `std::atomic<int32_t>` in `frame_trace.cpp` (exposed as `FrameTrace.nextFrameId()`) avoids that. We use `frameId` rather than "cookie" in our wrappers because we always pass a frame number; the term "cookie" is only kept where the underlying ATrace API uses it.
- **Superseded frames' async slices are closed at the supersede point, not left dangling.** This keeps the Perfetto UI clean (no slices extending to infinity). The closed-on-supersede durations are within ~1/N of completed frames so they barely perturb aggregate stats; the `dropped_frames` counter remains the authoritative drop count.
- **`dz.frame_render` is a sync section** because both begin and end happen on the render thread within a single function. Use sync (not async) when the section doesn't cross threads, doesn't overlap with others of the same name, and doesn't need per-instance identification — sync sections are slightly cheaper and don't need a cookie.
- **ATrace async APIs are API 29+; `minSdk` is 29 to match.** No weak-linking dance — `frame_trace.hpp` includes `<android/trace.h>` and calls `ATrace_beginAsyncSection` / `endAsyncSection` directly. If `minSdk` ever drops below 29 again, the helpers will need to come back as weak symbols.
- **`-a com.dz.camerafast` is mandatory** when invoking `perfetto`. Without it, app-tag atrace sections (which is where these slices land) are filtered out and the trace processor returns zero rows. Both helper scripts always pass it; if you ever invoke `perfetto` by hand, don't forget.
- **Profileable for release.** `AndroidManifest.xml` declares `<profileable android:shell="true"/>` inside `<application>` so Perfetto can attach to a release build without it being `debuggable`. Always measure on release (`installRelease`) — debug NDK builds add ~50–100% overhead to the C++ paths, which would skew the baseline.

## Tooling — how to actually use this

| What you want | Skill / Script |
|---|---|
| One-shot frame-latency measurement (single N-second capture) | `scripts/measure-frame-latency.sh [seconds]` |
| Establish a baseline with dispersion (5 × 10s by default, JSON output) | `scripts/baseline-frame-latency.sh` — invokable via `/frame-latency-baseline` |
| Build, install, launch, screenshot for visual verification of UI changes | `/verify-on-device` |
| Discover Android-platform skills (camera, performance, perfetto-sql, etc.) | `vendor/android-skills/` submodule |

All three scripts/skills assume a single ADB device. Set `ANDROID_SERIAL=<serial>` if multiple are attached. They auto-download Perfetto's `trace_processor` to `.cache/frame-latency/` (gitignored, ~25 MB) on first run.

## Build / install gotchas

These have bitten before; capture them so they don't bite again.

- **Use `installRelease` for any latency measurement** — debug builds add significant NDK overhead. The release build is signed with the debug keystore (`signingConfig signingConfigs.debug` in `app/build.gradle`) so it installs locally without provisioning a real keystore.
- **`assembleDebug` / `assembleRelease` both fail for `armeabi-v7a`** because the repo ships `libs/shaderc/c++_static/{arm64-v8a,x86_64}/libshaderc.a` but not the v7a copy. Always limit to a single ABI:
  ```bash
  ./gradlew :app:installRelease -Pandroid.injected.build.abi=$(adb shell getprop ro.product.cpu.abi | tr -d '\r')
  ```
- **Multi-display devices (foldables) corrupt `adb exec-out screencap`** by prefixing the PNG with a stdout warning. Pass `-d <display-id>` after discovering via `adb shell dumpsys SurfaceFlinger --display-id`. Trace capture is unaffected.
- **Camera permission is gated by activity finish-on-deny.** Pre-grant it (`adb shell pm grant com.dz.camerafast android.permission.CAMERA`) when scripting; the helper scripts already do.
- **Renderer state survives `am start`** without `force-stop`. The helper scripts always `force-stop` before launch, and `baseline-frame-latency.sh` also force-stops *between* runs so each iteration sees a cold app.

## Baseline findings (SM-F936B / Android 16, arm64-v8a, release build, 5 × 10 s)

These are the numbers a future PR should be diffed against. Headline metric is `dz.frame_e2e.<gl|vk>` — full camera-arrival → on-screen latency. JSON form at `.cache/frame-latency/baseline.json`.

| Metric | mean | CV% (across 5 runs) |
|---|---:|---:|
| `frame_e2e.gl.avg` | 13.25 ms | 1.8% |
| `frame_e2e.gl.p90` | 20.72 ms | 1.4% |
| `frame_e2e.gl.p99` | 23.45 ms | 1.3% |
| `frame_e2e.vk.avg` | 14.91 ms | 1.3% |
| `frame_e2e.vk.p90` | 22.48 ms | 1.0% |
| `frame_e2e.vk.p99` | 25.50 ms | 2.6% |
| `frame_to_screen.gl.p90` | 18.42 ms | 0.8% |
| `frame_to_screen.vk.p90` | 19.02 ms | 1.0% |
| `frame_render.gl.avg` | 0.57 ms | 6.4% |
| `frame_render.vk.avg` | 1.76 ms | 5.0% |
| `frame_native_proc.gl.avg` | 0.73 ms | 6.2% |
| `frame_native_proc.vk.avg` | 1.66 ms | 2.4% |

OpenGL is ~1.7 ms faster end-to-end on average (`frame_e2e` avg 13.25 vs 14.91), and ~1.8 ms faster at p90 (20.7 vs 22.5). The gap is split between `frame_render` (GL 0.57 vs VK 1.76) and `frame_native_proc` (GL 0.73 vs VK 1.66) — Vulkan's hwBuffer → vkImage path and `vkAcquireNextImageKHR + submit + fence wait` both cost more than the OpenGL equivalents.

**Most of `frame_to_screen` is vsync wait.** `frame_to_screen.gl.avg ≈ 10.9 ms` but only ~0.57 ms of that is actual GL command submission (`frame_render.gl.avg`); the remaining ~10.3 ms is Choreographer/vsync wait. Same shape for Vulkan: ~11.7 ms total vs ~1.76 ms of work. Optimizations that shave µs off GL/VK commands won't move the e2e needle until the vsync wait is what we're trying to displace (e.g. higher refresh rate, lower-latency presentation extensions).

**Which metrics to gate PRs on:**
- **Tight (±5%)**: `frame_e2e.{gl,vk}.{avg, p90, p99}`, `frame_to_screen.{gl,vk}.p90`. All sub-3% CV.
- **Looser (±10%)**: `frame_render.{gl,vk}.avg`, `frame_native_proc.{gl,vk}.avg`. CV 2–7%.
- **Watch only, no gate**: `frame_native_proc.{gl,vk}.{p90, p99}` and `frame_render.{gl,vk}.{p90, p99}` (5–25% CV — single-tail-sample noise).
- **Skip entirely**: every `max` (single-outlier sensitive, 15–40% CV), and `p50` on screen-facing stages (bimodal — submit-to-vsync alignment).

Slice counts are deterministic to within ±1 per 10 s window: ~298 frames per renderer (~30 fps from camera). A meaningful deviation in count is itself a regression signal.

## Planned next steps (not yet implemented)

- **Macrobenchmark module** wrapping the same capture flow with `TraceSectionMetric`, so the run produces the JSON straight from a Gradle task rather than a bash wrapper. The `testing/testing-setup` skill in `vendor/android-skills/` is the entry point for scaffolding.
- **CI gate via GitHub Actions** running the macrobenchmark on either Firebase Test Lab (real hardware, paid per device-minute) or Gradle Managed Devices (emulator on the GHA runner, free but GPU≠real). Likely GMD for speed, with periodic FTL runs for trend tracking. The PR check diffs against a `baseline.json` checked into the repo and fails on regressions outside the gates listed above.

## Other tooling worth knowing about

- `vendor/android-skills/` — Google's official AI-optimized SKILL.md files for Android dev (Perfetto SQL, edge-to-edge, AGP upgrades, R8 analyzer, Macrobenchmark testing-setup, etc.). Browse `vendor/android-skills/<topic>/<skill>/SKILL.md` rather than guessing — every skill there has a precise scope statement at the top.
- `vendor/jni.hpp/` — submodule for the Mapbox `jni.hpp` C++/JNI wrapper. Used by `core_engine.{hpp,cpp}` (native peer) and `frame_trace.{hpp,cpp}` (static-only via `STATIC_METHOD` from `util.hpp`).
- `local.properties` is gitignored; the `sdk.dir` line is required for Gradle to find your SDK.
