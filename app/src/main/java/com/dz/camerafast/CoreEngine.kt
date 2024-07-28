package com.dz.camerafast

import android.graphics.PixelFormat
import android.hardware.HardwareBuffer
import android.util.Log
import android.view.Surface
import android.view.SurfaceHolder
import androidx.annotation.Keep

@Keep
class CoreEngine(
    val renderingMode: RenderingMode,
) : SurfaceHolder.Callback {

    internal var surfaceHolder: SurfaceHolder? = null
        set(value) {
            field = value
            // we will use RGBA_8888 here and use same config in render thread
            field?.setFormat(PixelFormat.RGBA_8888)
            field?.addCallback(this)
        }

    init {
        initialize(renderingMode.ordinal)
    }

    fun sendCameraFrame(buffer: HardwareBuffer, rotationDegrees: Int, backCamera: Boolean) {
        buffer.printSupportedUsageFlags()
        nativeSendCameraFrame(buffer, rotationDegrees, backCamera)
    }

    override fun surfaceCreated(p0: SurfaceHolder) {
        // do nothing
    }

    override fun surfaceChanged(holder: SurfaceHolder, format: Int, width: Int, height: Int) {
        Log.i(TAG, "Surface changed ${holder.surface}, format $format, width $width, height $height")
        nativeSetSurface(holder.surface, width, height)
    }

    override fun surfaceDestroyed(p0: SurfaceHolder) {
        nativeSetSurface(null, 0 ,0)
    }

    fun destroy() {
        nativeDestroy()
    }

    @Suppress("unused")
    @Keep
    @Volatile
    var peer: Long = 0

    private external fun nativeSetSurface(surface: Surface?, width: Int, height: Int)

    private external fun nativeSendCameraFrame(buffer: HardwareBuffer, rotationDegrees: Int, backCamera: Boolean)

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

        init {
            System.loadLibrary("native-engine")
        }
    }
}