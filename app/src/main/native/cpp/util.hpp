#pragma once

#include <android/log.h>

#define METHOD(MethodPtr, name) jni::MakeNativePeerMethod<decltype(MethodPtr), (MethodPtr)>(name)
#define LOGD(...) __android_log_print(ANDROID_LOG_DEBUG,"CoreEngineNative",__VA_ARGS__)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR,"CoreEngineNative",__VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,"CoreEngineNative",__VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,"CoreEngineNative",__VA_ARGS__)
