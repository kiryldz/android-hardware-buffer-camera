#include "frame_trace.hpp"

#include <string>

namespace engine {
namespace android {

std::atomic<int32_t> FrameTrace::frameCounter{0};

jni::jint FrameTrace::nextFrameId(jni::JNIEnv &, jni::Class<FrameTrace> &) {
  return ++frameCounter;
}

void FrameTrace::traceBeginAsync(jni::JNIEnv &env, jni::Class<FrameTrace> &,
                                 const jni::String &name, jni::jint frameId) {
  ::engine::android::traceBeginAsync(jni::Make<std::string>(env, name).c_str(), frameId);
}

void FrameTrace::traceEndAsync(jni::JNIEnv &env, jni::Class<FrameTrace> &,
                               const jni::String &name, jni::jint frameId) {
  ::engine::android::traceEndAsync(jni::Make<std::string>(env, name).c_str(), frameId);
}

jni::Local<jni::String> FrameTrace::frameE2EGlName(jni::JNIEnv &env, jni::Class<FrameTrace> &) {
  return jni::Make<jni::String>(env, std::string(traceNames::FRAME_E2E_GL));
}

jni::Local<jni::String> FrameTrace::frameE2EVkName(jni::JNIEnv &env, jni::Class<FrameTrace> &) {
  return jni::Make<jni::String>(env, std::string(traceNames::FRAME_E2E_VK));
}

jni::Local<jni::String> FrameTrace::frameToNativeGlName(jni::JNIEnv &env, jni::Class<FrameTrace> &) {
  return jni::Make<jni::String>(env, std::string(traceNames::FRAME_TO_NATIVE_GL));
}

jni::Local<jni::String> FrameTrace::frameToNativeVkName(jni::JNIEnv &env, jni::Class<FrameTrace> &) {
  return jni::Make<jni::String>(env, std::string(traceNames::FRAME_TO_NATIVE_VK));
}

} // namespace android
} // namespace engine
