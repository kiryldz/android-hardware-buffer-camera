#pragma once

#include <atomic>
#include <cstdint>

#include <android/trace.h>
#include <jni/jni.hpp>

#include "util.hpp"

namespace engine {
namespace android {

inline void traceBeginAsync(const char *name, int32_t frameId) {
  ATrace_beginAsyncSection(name, frameId);
}

inline void traceEndAsync(const char *name, int32_t frameId) {
  ATrace_endAsyncSection(name, frameId);
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
constexpr const char *FRAME_RENDER_GL = "dz.frame_render.gl";
constexpr const char *FRAME_RENDER_VK = "dz.frame_render.vk";
} // namespace traceNames

class FrameTrace {

public:
  static constexpr auto Name() { return "com/dz/camerafast/FrameTrace"; }

  static void registerNatives(JNIEnv &env) {
    jni::RegisterNatives(env, *jni::Class<FrameTrace>::Find(env),
            STATIC_METHOD(&FrameTrace::nextFrameId, "nextFrameId"),
            STATIC_METHOD(&FrameTrace::traceBeginAsync, "traceBeginAsync"),
            STATIC_METHOD(&FrameTrace::frameE2EGlName, "frameE2EGlName"),
            STATIC_METHOD(&FrameTrace::frameE2EVkName, "frameE2EVkName"),
            STATIC_METHOD(&FrameTrace::frameToNativeGlName, "frameToNativeGlName"),
            STATIC_METHOD(&FrameTrace::frameToNativeVkName, "frameToNativeVkName")
    );
  }

  static jni::jint nextFrameId(jni::JNIEnv &env, jni::Class<FrameTrace> &);
  static void traceBeginAsync(jni::JNIEnv &env, jni::Class<FrameTrace> &, const jni::String &name, jni::jint frameId);
  static jni::Local<jni::String> frameE2EGlName(jni::JNIEnv &env, jni::Class<FrameTrace> &);
  static jni::Local<jni::String> frameE2EVkName(jni::JNIEnv &env, jni::Class<FrameTrace> &);
  static jni::Local<jni::String> frameToNativeGlName(jni::JNIEnv &env, jni::Class<FrameTrace> &);
  static jni::Local<jni::String> frameToNativeVkName(jni::JNIEnv &env, jni::Class<FrameTrace> &);

private:
  static std::atomic<int32_t> frameCounter;
};

} // namespace android
} // namespace engine
