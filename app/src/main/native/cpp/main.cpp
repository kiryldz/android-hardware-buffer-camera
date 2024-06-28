#include <jni/jni.hpp>

#include "core_engine.hpp"

extern "C" JNIEXPORT jint

JNICALL JNI_OnLoad(JavaVM *vm, void *) {
  jni::JNIEnv &env = jni::GetEnv(*vm);
  engine::android::CoreEngine::registerNatives(env);
  return JNI_VERSION_1_6;
}