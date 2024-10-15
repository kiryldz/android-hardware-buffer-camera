#include <jni/jni.hpp>

#include "rendering_engine.hpp"

extern "C" JNIEXPORT jint

JNICALL JNI_OnLoad(JavaVM *vm, void *) {
  jni::JNIEnv &env = jni::GetEnv(*vm);
  engine::android::RenderingEngine::registerNatives(env);
  return JNI_VERSION_1_6;
}