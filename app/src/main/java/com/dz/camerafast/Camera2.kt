package com.dz.camerafast

import android.annotation.SuppressLint
import android.content.Context
import android.graphics.ImageFormat
import android.hardware.HardwareBuffer
import android.hardware.camera2.CameraCaptureSession
import android.hardware.camera2.CameraCaptureSession.StateCallback
import android.hardware.camera2.CameraCharacteristics
import android.hardware.camera2.CameraDevice
import android.hardware.camera2.CameraManager
import android.media.ImageReader
import android.os.Build
import android.os.Handler
import android.os.HandlerThread
import android.util.Log
import androidx.annotation.RequiresApi
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.ui.platform.LocalContext
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlinx.coroutines.withContext
import kotlin.coroutines.resume

@RequiresApi(Build.VERSION_CODES.Q)
@Composable
fun Camera2(
    coreEngines: List<CoreEngine>,
    lensFacing: Int,
) {
    val context = LocalContext.current
    val coroutineScopeMain = rememberCoroutineScope()
    val pixelFormat = ImageFormat.YUV_420_888

    val cameraThread = HandlerThread("CameraThread").apply { start() }
    val cameraHandler = Handler(cameraThread.looper)

    val cameraManager: CameraManager by lazy {
        context.applicationContext.getSystemService(Context.CAMERA_SERVICE) as CameraManager
    }

    LaunchedEffect(lensFacing) {
        val cameraData = openCamera(cameraManager, lensFacing, coroutineScopeMain, cameraHandler)!!
        val size = cameraData.second.get(
            CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP
        )!!
            .getOutputSizes(pixelFormat).maxByOrNull { it.height * it.width }!!
        val imageReader = ImageReader.newInstance(
            size.width,
            size.height,
            pixelFormat,
            IMAGE_BUFFER_SIZE,
            HardwareBuffer.USAGE_GPU_SAMPLED_IMAGE
        )
        val cameraCaptureSession = suspendCancellableCoroutine {
            cameraData.first.createCaptureSession(
                listOf(imageReader.surface),
                object : StateCallback() {
                    override fun onConfigured(p0: CameraCaptureSession) {
                        it.resume(p0)
                    }

                    override fun onConfigureFailed(p0: CameraCaptureSession) {
                        Log.e(TAG, "onConfigureFailed")
                        it.resume(null)
                    }
                },
                cameraHandler
            )
        }
        cameraCaptureSession?.setRepeatingRequest(
            cameraData.first
                .createCaptureRequest(CameraDevice.TEMPLATE_PREVIEW).apply { addTarget(imageReader.surface) }
                .build(),
            null,
            cameraHandler
        )
        imageReader.setOnImageAvailableListener(
            {
                val image = it.acquireLatestImage()
                Log.e(TAG, "Camera2: ${image.hashCode()}")
                coreEngines.forEach { engine ->
                    if (engine.renderingMode == RenderingMode.VULKAN) {
                        engine.sendCameraFrame(image.hardwareBuffer!!, 270, false)
                    }
                }
                image.close()
            },
            cameraHandler
        )
    }
}

@SuppressLint("MissingPermission")
private suspend fun openCamera(
    cameraManager: CameraManager,
    lensFacing: Int,
    coroutineScope: CoroutineScope,
    handler: Handler,
): Pair<CameraDevice, CameraCharacteristics>? = withContext(coroutineScope.coroutineContext) {
    cameraManager.cameraIdList.forEach { cameraId ->
        val characteristics = cameraManager.getCameraCharacteristics(cameraId)
        if (characteristics.get(CameraCharacteristics.LENS_FACING) == lensFacing) {
            return@withContext suspendCancellableCoroutine {
                cameraManager.openCamera(cameraId, object : CameraDevice.StateCallback() {
                    override fun onOpened(device: CameraDevice) =
                        it.resume(device to characteristics)

                    override fun onDisconnected(device: CameraDevice) {
                        Log.w(TAG, "Camera $cameraId has been disconnected")
                        it.resume(null)
                    }

                    override fun onError(device: CameraDevice, error: Int) {
                        val msg = when (error) {
                            ERROR_CAMERA_DEVICE -> "Fatal (device)"
                            ERROR_CAMERA_DISABLED -> "Device policy"
                            ERROR_CAMERA_IN_USE -> "Camera in use"
                            ERROR_CAMERA_SERVICE -> "Fatal (service)"
                            ERROR_MAX_CAMERAS_IN_USE -> "Maximum cameras in use"
                            else -> "Unknown"
                        }
                        val exc = RuntimeException("Camera $cameraId error: ($error) $msg")
                        Log.e(TAG, exc.message, exc)
                        it.resume(null)
                    }
                }, handler)
            }
        }
    }
    return@withContext null
}

private const val TAG = "DzCamera2"

/** Maximum number of images that will be held in the reader's buffer */
private const val IMAGE_BUFFER_SIZE: Int = 3