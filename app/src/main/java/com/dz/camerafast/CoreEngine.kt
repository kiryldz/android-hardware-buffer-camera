package com.dz.camerafast

import android.graphics.PixelFormat
import android.hardware.HardwareBuffer
import android.util.Log
import android.view.Surface
import android.view.SurfaceHolder
import androidx.annotation.Keep

@Keep
class CoreEngine(
    surfaceHolder: SurfaceHolder,
    renderingMode: RenderingMode,
) : SurfaceHolder.Callback {

    init {
        // we will use RGBA_8888 here and use same config in render thread
        surfaceHolder.setFormat(PixelFormat.RGBA_8888)
        System.loadLibrary("native-engine")
        initialize(renderingMode.ordinal)
        surfaceHolder.addCallback(this)
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

    fun feedHardwareBuffer(buffer: HardwareBuffer, rotationDegrees: Int) {
        nativeFeedHardwareBuffer(buffer, rotationDegrees)
    }

    fun destroy() {
        nativeDestroy()
    }

    @Suppress("unused")
    @Keep
    @Volatile
    var peer: Long = 0

    private external fun nativeSetSurface(surface: Surface?, width: Int, height: Int)

    private external fun nativeFeedHardwareBuffer(buffer: HardwareBuffer, rotationDegrees: Int)

    private external fun nativeDestroy()

    private external fun initialize(mode: Int)

    protected external fun finalize()

    private companion object {
        private const val TAG = "DzCoreKotlin"
    }
}