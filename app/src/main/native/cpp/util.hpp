#pragma once

#include <android/log.h>

#define METHOD(MethodPtr, name) jni::MakeNativePeerMethod<decltype(MethodPtr), (MethodPtr)>(name)
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR,"DzCoreNative",__VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN,"DzCoreNative",__VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,"DzCoreNative",__VA_ARGS__)


// Vulkan call wrapper
#define CALL_VK(func)                                                 \
  if (VK_SUCCESS != (func)) {                                         \
    __android_log_print(ANDROID_LOG_ERROR, "DzCoreNative",            \
                        "Vulkan error. File[%s], line[%d]", __FILE__, \
                        __LINE__);                                    \
    assert(false);                                                    \
  }

// A macro to check value is VK_SUCCESS
// Used also for non-vulkan functions but return VK_SUCCESS
#define VK_CHECK(x) CALL_VK(x)