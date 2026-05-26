#pragma once

#include <cstdint>

#include <android/trace.h>

namespace engine {
namespace android {

inline void traceBeginAsync(const char *name, int32_t cookie) {
  ATrace_beginAsyncSection(name, cookie);
}

inline void traceEndAsync(const char *name, int32_t cookie) {
  ATrace_endAsyncSection(name, cookie);
}

inline void traceSetCounter(const char *name, int64_t value) {
  ATrace_setCounter(name, value);
}

inline void traceBeginSync(const char *name) {
  ATrace_beginSection(name);
}

inline void traceEndSync() {
  ATrace_endSection();
}

// Stable section names — must match com.dz.camerafast.FrameTrace on the Kotlin side.
namespace traceNames {
constexpr const char *FRAME_TO_NATIVE_GL = "dz.frame_to_native.gl";
constexpr const char *FRAME_TO_NATIVE_VK = "dz.frame_to_native.vk";
constexpr const char *FRAME_NATIVE_PROC_GL = "dz.frame_native_proc.gl";
constexpr const char *FRAME_NATIVE_PROC_VK = "dz.frame_native_proc.vk";
constexpr const char *FRAME_TO_SCREEN_GL = "dz.frame_to_screen.gl";
constexpr const char *FRAME_TO_SCREEN_VK = "dz.frame_to_screen.vk";
constexpr const char *FRAME_E2E_GL = "dz.frame_e2e.gl";
constexpr const char *FRAME_E2E_VK = "dz.frame_e2e.vk";
constexpr const char *DROPPED_FRAMES_GL = "dz.dropped_frames.gl";
constexpr const char *DROPPED_FRAMES_VK = "dz.dropped_frames.vk";
// Inner sync slice covering only the renderImpl work (draw+swap/present), so
// frame_to_screen - frame_render approximates the Choreographer/vsync wait.
constexpr const char *FRAME_RENDER_GL = "dz.frame_render.gl";
constexpr const char *FRAME_RENDER_VK = "dz.frame_render.vk";
} // namespace traceNames

} // namespace android
} // namespace engine
