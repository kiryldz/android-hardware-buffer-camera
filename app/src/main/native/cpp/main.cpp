#include <jni/jni.hpp>

#include "core_engine.hpp"
#include "frame_trace.hpp"

extern "C" JNIEXPORT jint

JNICALL JNI_OnLoad(JavaVM *vm, void *) {
  jni::JNIEnv &env = jni::GetEnv(*vm);
  engine::android::CoreEngine::registerNatives(env);
  engine::android::FrameTrace::registerNatives(env);
  return JNI_VERSION_1_6;
}