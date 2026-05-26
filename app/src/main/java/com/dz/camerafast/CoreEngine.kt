package com.dz.camerafast

import android.graphics.SurfaceTexture
import android.hardware.HardwareBuffer
import android.util.Log
import android.view.Surface
import android.view.TextureView
import androidx.annotation.Keep

@Keep
class CoreEngine(
  val renderingMode: RenderingMode,
) : TextureView.SurfaceTextureListener {

  private var surface: Surface? = null

  internal var textureView: TextureView? = null
    set(value) {
      field?.surfaceTextureListener = null
      field = value
      field?.surfaceTextureListener = this
      // If the TextureView is already available, the framework won't fire
      // onSurfaceTextureAvailable — invoke it ourselves.
      if (value?.isAvailable == true) {
        value.surfaceTexture?.let { texture ->
          onSurfaceTextureAvailable(texture, value.width, value.height)
        }
      }
    }

  init {
    initialize(renderingMode.ordinal)
  }

  fun sendCameraFrame(buffer: HardwareBuffer, rotationDegrees: Int, backCamera: Boolean) {
    buffer.printSupportedUsageFlags()
    nativeSendCameraFrame(buffer, rotationDegrees, backCamera)
  }

  private var previousWeight: Float = 1.0f

  /** Pushes the current preview weight; triggers a native refit at threshold crossings only. */
  fun refit(weight: Float): Boolean {
    val triggered = shouldRefit(previousWeight, weight)
    previousWeight = weight
    if (triggered) {
      nativeRefit()
    }
    return triggered
  }

  override fun onSurfaceTextureAvailable(surfaceTexture: SurfaceTexture, width: Int, height: Int) {
    Log.i(TAG, "Surface texture available, width $width, height $height")
    // Release any stale Surface from a previous binding before overwriting the field.
    surface?.release()
    surface = Surface(surfaceTexture).also { nativeSetSurface(it, width, height) }
  }

  override fun onSurfaceTextureSizeChanged(surfaceTexture: SurfaceTexture, width: Int, height: Int) {
    Log.i(TAG, "Surface texture size changed, width $width, height $height")
    nativeUpdateWindowSize(width, height)
  }

  override fun onSurfaceTextureDestroyed(surfaceTexture: SurfaceTexture): Boolean {
    nativeSetSurface(null, 0, 0)
    surface?.release()
    surface = null
    // Drop the TextureView reference so the destroyed view can be GC'd. Identity-guard against
    // stale callbacks from an older SurfaceTexture.
    if (textureView?.surfaceTexture === surfaceTexture) {
      textureView = null
    }
    // true = let TextureView release the SurfaceTexture for us.
    return true
  }

  override fun onSurfaceTextureUpdated(surfaceTexture: SurfaceTexture) {
    // do nothing
  }

  fun destroy() {
    nativeDestroy()
  }

  @Suppress("unused")
  @Keep
  @Volatile
  var peer: Long = 0

  private external fun nativeSetSurface(surface: Surface?, width: Int, height: Int)

  private external fun nativeUpdateWindowSize(width: Int, height: Int)

  private external fun nativeSendCameraFrame(
    buffer: HardwareBuffer,
    rotationDegrees: Int,
    backCamera: Boolean
  )

  private external fun nativeRefit()

  private external fun nativeDestroy()

  private external fun initialize(mode: Int)

  private external fun finalize()

  private fun HardwareBuffer.printSupportedUsageFlags() {
    val usage = usage.toInt()
    val supportedUsages = mutableListOf<String>()

    if (usage and HardwareBuffer.USAGE_CPU_READ_RARELY.toInt() != 0) {
      supportedUsages.add("USAGE_CPU_READ_RARELY")
    }
    if (usage and HardwareBuffer.USAGE_CPU_READ_OFTEN.toInt() != 0) {
      supportedUsages.add("USAGE_CPU_READ_OFTEN")
    }
    if (usage and HardwareBuffer.USAGE_CPU_WRITE_RARELY.toInt() != 0) {
      supportedUsages.add("USAGE_CPU_WRITE_RARELY")
    }
    if (usage and HardwareBuffer.USAGE_CPU_WRITE_OFTEN.toInt() != 0) {
      supportedUsages.add("USAGE_CPU_WRITE_OFTEN")
    }
    if (usage and HardwareBuffer.USAGE_GPU_SAMPLED_IMAGE.toInt() != 0) {
      supportedUsages.add("USAGE_GPU_SAMPLED_IMAGE")
    }
    if (usage and HardwareBuffer.USAGE_GPU_COLOR_OUTPUT.toInt() != 0) {
      supportedUsages.add("USAGE_GPU_COLOR_OUTPUT")
    }
    if (usage and HardwareBuffer.USAGE_GPU_CUBE_MAP.toInt() != 0) {
      supportedUsages.add("USAGE_GPU_CUBE_MAP")
    }
    if (usage and HardwareBuffer.USAGE_GPU_MIPMAP_COMPLETE.toInt() != 0) {
      supportedUsages.add("USAGE_GPU_MIPMAP_COMPLETE")
    }
    if (usage and HardwareBuffer.USAGE_PROTECTED_CONTENT.toInt() != 0) {
      supportedUsages.add("USAGE_PROTECTED_CONTENT")
    }
    if (usage and HardwareBuffer.USAGE_SENSOR_DIRECT_DATA.toInt() != 0) {
      supportedUsages.add("USAGE_SENSOR_DIRECT_DATA")
    }
    if (usage and HardwareBuffer.USAGE_VIDEO_ENCODE.toInt() != 0) {
      supportedUsages.add("USAGE_VIDEO_ENCODE")
    }
    Log.i(CameraActivity.TAG, "Supports ${supportedUsages.joinToString(", ")}")
  }

  private companion object {
    private const val TAG = "DzCoreKotlin"

    private val REFIT_THRESHOLDS = floatArrayOf(0.1f, 0.5f)

    private fun shouldRefit(prev: Float, curr: Float): Boolean {
      for (threshold in REFIT_THRESHOLDS) {
        val crossedUp = prev < threshold && curr >= threshold
        val crossedDown = prev > threshold && curr <= threshold
        if (crossedUp || crossedDown) return true
      }
      val settledAtZero = curr <= 0.0f && prev > 0.0f
      val settledAtOne = curr >= 1.0f && prev < 1.0f
      return settledAtZero || settledAtOne
    }

    init {
      System.loadLibrary("native-engine")
    }
  }
}