package com.dz.camerafast

import android.os.Trace
import java.util.concurrent.atomic.AtomicInteger

/**
 * Per-frame Perfetto/atrace instrumentation. Section names follow the convention
 * `dz.<stage>.<gl|vk>` so each renderer is metered independently.
 *
 * Frame ids are a process-wide monotonic Int counter (not the sensor timestamp) because
 * android.os.Trace cookies are 32-bit. The Int wraps after ~2.1B frames, which we never reach.
 */
object FrameTrace {

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
