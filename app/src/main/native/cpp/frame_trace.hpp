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
} // namespace traceNames

} // namespace android
} // namespace engine
