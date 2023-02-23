#pragma once

#include <android/hardware_buffer.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>

namespace engine {
namespace android {

class BaseRenderer {

public:
  virtual void setWindow(ANativeWindow * window) = 0;
  virtual void updateWindowSize(int width, int height) = 0;
  virtual void resetWindow() = 0;
  /**
   * Always called from camera worker thread - feed new camera buffer.
   * @param aHardwareBuffer
   */
  virtual void feedHardwareBuffer(AHardwareBuffer * aHardwareBuffer) = 0;
};

} // namespace android
} // namespace engine
