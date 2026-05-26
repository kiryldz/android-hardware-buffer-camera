# CLAUDE.md — Camera Fast

Project-wide notes for future sessions. Captures non-obvious context — architecture intent, instrumentation contracts, build gotchas, baseline findings. Read this before diving into code, especially before touching the camera→GPU pipeline or the build.

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
- `app/src/main/native/cpp/base_renderer.{hpp,cpp}` — render-thread Looper, frame-task scheduling, viewport/MVP coalescing, the `pendingPresentCookie` interlock.
- `app/src/main/native/cpp/{opengl,vulkan}_renderer.{hpp,cpp}` — backend-specific code; the only place EGL/GL or Vulkan symbols are touched.

The CPU fallback in `nativeSendCameraFrame` exists because **CameraX**'s `ImageAnalysis` delivers HW buffers with `USAGE_CPU_*` flags rather than `USAGE_GPU_SAMPLED_IMAGE`. We re-allocate a GPU buffer once, `memcpy` into it each frame (drops the engine mutex during the slow memcpy so main-thread JNI calls aren't blocked), then hand it to the renderer. Camera2 with `ImageFormat.PRIVATE` + `USAGE_GPU_SAMPLED_IMAGE` skips that copy entirely — that's the point of supporting both APIs.

## Frame-timing instrumentation contract

Per camera frame, the analyzer assigns a process-wide monotonic `frameId: Int` via `FrameTrace.nextFrameId()`. That id is the **cookie** of four ATrace async sections per renderer:

| Section | Begin (where) | End (where) | What it measures |
|---|---|---|---|
| `dz.frame_to_native.<gl\|vk>` | Kotlin analyzer | `BaseRenderer::processCameraFrame` JNI entry | Kotlin glue overhead |
| `dz.frame_native_proc.<gl\|vk>` | `processCameraFrame` entry | end of `hwBufferToTexture` on render thread | Native processing |
| `dz.frame_to_screen.<gl\|vk>` | end of `hwBufferToTexture` | after `eglSwapBuffers` / `vkQueuePresentKHR` | Submit→present |
| `dz.frame_e2e.<gl\|vk>` | Kotlin analyzer (same point as `to_native` begin) | swap/present returned | **End-to-end** |

Section name constants live in two places that must stay in sync:
- Kotlin: `app/src/main/java/com/dz/camerafast/FrameTrace.kt`
- C++:    `app/src/main/native/cpp/frame_trace.hpp`

Design decisions worth remembering:
- **Cookie is an `Int`, not the sensor timestamp.** `android.os.Trace` cookies are 32-bit; sensor timestamps in nanoseconds overflow. A simple `AtomicInteger` counter avoids the collision risk that low-32-bit truncation would create.
- **Superseded frames are deliberately dropped from `to_screen` / `e2e`.** When a newer frame's `hwBufferToTexture` overwrites `pendingPresentCookie` before Choreographer fires, the older frame's slices are left dangling — they won't appear in `slice` rows and won't pollute the latency distribution. By design.
- **ATrace async APIs are API 29+; `minSdk` is 28.** `frame_trace.hpp` declares the symbols with `__attribute__((weak))`. On API 28 they resolve to `nullptr` and the helpers no-op cleanly; on 29+ the dynamic linker fills them in from `libandroid.so`.
- **`-a com.dz.camerafast` is mandatory** when invoking `perfetto`. Without it, app-tag atrace sections (which is where these slices land) are filtered out and the trace processor returns zero rows. Both helper scripts always pass it; if you ever invoke `perfetto` by hand, don't forget.

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

- **`assembleDebug` fails for `armeabi-v7a`** because the repo ships `libs/shaderc/c++_static/{arm64-v8a,x86_64}/libshaderc.a` but not the v7a copy. Always limit to a single ABI:
  ```bash
  ./gradlew :app:installDebug -Pandroid.injected.build.abi=$(adb shell getprop ro.product.cpu.abi | tr -d '\r')
  ```
- **Multi-display devices (foldables) corrupt `adb exec-out screencap`** by prefixing the PNG with a stdout warning. Pass `-d <display-id>` after discovering via `adb shell dumpsys SurfaceFlinger --display-id`. Trace capture is unaffected.
- **Camera permission is gated by activity finish-on-deny.** Pre-grant it (`adb shell pm grant com.dz.camerafast android.permission.CAMERA`) when scripting; the helper scripts already do.
- **Renderer state survives `am start`** without `force-stop`. The helper scripts always `force-stop` before launch, and `baseline-frame-latency.sh` also force-stops *between* runs so each iteration sees a cold app.

## Baseline findings (SM-F936B / Android 16, arm64-v8a, 5 × 10 s)

These are the numbers a future PR should be diffed against. Headline metric is `dz.frame_e2e.<gl|vk>` — full camera-arrival → on-screen latency.

| Metric | mean | CV% (across 5 runs) |
|---|---:|---:|
| `frame_e2e.gl.avg` | 13.05 ms | 2.4% |
| `frame_e2e.gl.p90` | 20.62 ms | 0.8% |
| `frame_e2e.gl.p99` | 24.04 ms | 3.4% |
| `frame_e2e.vk.avg` | 17.38 ms | 1.4% |
| `frame_e2e.vk.p90` | 25.27 ms | 1.5% |
| `frame_e2e.vk.p99` | 29.31 ms | 5.0% |
| `frame_to_screen.gl.p90` | 17.79 ms | 1.3% |
| `frame_to_screen.vk.p90` | 20.12 ms | 0.8% |

OpenGL is consistently ~4 ms faster end-to-end (`frame_e2e` p90 20.6 ms vs 25.3 ms). Most of e2e is `frame_to_screen` — i.e. wait for the next vsync — so the GL/VK gap mainly reflects `frame_native_proc.vk.avg ≈ 3.2 ms` vs `gl.avg ≈ 1.1 ms` (Vulkan's hwBuffer → vkImage path has more setup).

**Which metrics to gate PRs on:**
- **Tight (±5%)**: `frame_e2e.{gl,vk}.{avg, p90}`, `frame_to_screen.{gl,vk}.p90`. All sub-2% CV.
- **Looser (±10%)**: `frame_e2e.{gl,vk}.p99`, `frame_native_proc.{gl,vk}.avg`. CV 3–7%.
- **Watch only, no gate**: `frame_native_proc.{gl,vk}.{p90, p99}` (6–16% CV).
- **Skip entirely**: every `max` (single-outlier sensitive, 10–30% CV), and `p50` on screen-facing stages (bimodal — submit-to-vsync alignment).

Slice counts are deterministic to within ±1 per 10 s window: ~298 frames per renderer (~30 fps from camera). A meaningful deviation in count is itself a regression signal — the renderer dropped frames before present.

## Planned next steps (not yet implemented)

- **Macrobenchmark module** wrapping the same capture flow with `TraceSectionMetric`, so the run produces the JSON straight from a Gradle task rather than a bash wrapper. The `testing/testing-setup` skill in `vendor/android-skills/` is the entry point for scaffolding.
- **CI gate via GitHub Actions** running the macrobenchmark on either Firebase Test Lab (real hardware, paid per device-minute) or Gradle Managed Devices (emulator on the GHA runner, free but GPU≠real). Likely GMD for speed, with periodic FTL runs for trend tracking. The PR check diffs against a `baseline.json` checked into the repo and fails on regressions outside the gates listed above.

## Other tooling worth knowing about

- `vendor/android-skills/` — Google's official AI-optimized SKILL.md files for Android dev (Perfetto SQL, edge-to-edge, AGP upgrades, R8 analyzer, Macrobenchmark testing-setup, etc.). Browse `vendor/android-skills/<topic>/<skill>/SKILL.md` rather than guessing — every skill there has a precise scope statement at the top.
- `vendor/jni.hpp/` — submodule for the Mapbox `jni.hpp` C++/JNI wrapper used by `core_engine.{hpp,cpp}`.
- `local.properties` is gitignored; the `sdk.dir` line is required for Gradle to find your SDK.
