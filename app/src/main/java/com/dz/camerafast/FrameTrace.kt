package com.dz.camerafast

import android.os.Trace
import java.util.concurrent.atomic.AtomicInteger

object FrameTrace {

  // Trace cookies are 32-bit; sensor timestamps overflow Int. Plain counter instead.
  private val nextId = AtomicInteger(0)

  fun nextFrameId(): Int = nextId.incrementAndGet()

  fun e2eName(mode: RenderingMode): String = "dz.frame_e2e.${mode.suffix}"
  fun toNativeName(mode: RenderingMode): String = "dz.frame_to_native.${mode.suffix}"

  fun beginE2E(mode: RenderingMode, frameId: Int) {
    Trace.beginAsyncSection(e2eName(mode), frameId)
  }

  fun beginToNative(mode: RenderingMode, frameId: Int) {
    Trace.beginAsyncSection(toNativeName(mode), frameId)
  }

  private val RenderingMode.suffix: String
    get() = when (this) {
      RenderingMode.OPEN_GL_ES -> "gl"
      RenderingMode.VULKAN -> "vk"
    }
}
