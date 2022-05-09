#pragma once

#include <android/log.h>

#define METHOD(MethodPtr, name) jni::MakeNativePeerMethod<decltype(MethodPtr), (MethodPtr)>(name)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR,"DzCoreNative",__VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,"DzCoreNative",__VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,"DzCoreNative",__VA_ARGS__)
