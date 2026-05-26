#pragma once

#include <cstdint>

namespace engine {
namespace android {

// Weak-linked so the shared library still loads on API 28 (project minSdk).
// On API 29+ the dynamic linker resolves these to the real ATrace functions in libandroid.so.
extern "C" {
__attribute__((weak)) void ATrace_beginAsyncSection(const char *sectionName, int32_t cookie);
__attribute__((weak)) void ATrace_endAsyncSection(const char *sectionName, int32_t cookie);
}

inline void traceBeginAsync(const char *name, int32_t cookie) {
  if (&ATrace_beginAsyncSection) ATrace_beginAsyncSection(name, cookie);
}

inline void traceEndAsync(const char *name, int32_t cookie) {
  if (&ATrace_endAsyncSection) ATrace_endAsyncSection(name, cookie);
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
