package com.dz.camerafast

import androidx.annotation.Keep

@Keep
object FrameTrace {

  init {
    System.loadLibrary("native-engine")
  }

  @JvmField val FRAME_E2E_GL: String = frameE2EGlName()
  @JvmField val FRAME_E2E_VK: String = frameE2EVkName()
  @JvmField val FRAME_TO_NATIVE_GL: String = frameToNativeGlName()
  @JvmField val FRAME_TO_NATIVE_VK: String = frameToNativeVkName()

  @JvmStatic external fun nextFrameId(): Int
  @JvmStatic external fun traceBeginAsync(name: String, frameId: Int)
  @JvmStatic external fun traceEndAsync(name: String, frameId: Int)

  fun beginE2E(mode: RenderingMode, frameId: Int) =
    traceBeginAsync(if (mode == RenderingMode.VULKAN) FRAME_E2E_VK else FRAME_E2E_GL, frameId)

  fun beginToNative(mode: RenderingMode, frameId: Int) =
    traceBeginAsync(if (mode == RenderingMode.VULKAN) FRAME_TO_NATIVE_VK else FRAME_TO_NATIVE_GL, frameId)

  @JvmStatic private external fun frameE2EGlName(): String
  @JvmStatic private external fun frameE2EVkName(): String
  @JvmStatic private external fun frameToNativeGlName(): String
  @JvmStatic private external fun frameToNativeVkName(): String
}
