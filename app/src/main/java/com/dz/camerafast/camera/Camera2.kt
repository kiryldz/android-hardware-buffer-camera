package com.dz.camerafast.camera

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
import androidx.camera.core.CameraSelector
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.setValue
import androidx.compose.ui.platform.LocalContext
import androidx.lifecycle.compose.LifecycleStartEffect
import com.dz.camerafast.CameraActivity.Companion.TAG
import com.dz.camerafast.RenderingEngine
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.launch
import kotlinx.coroutines.suspendCancellableCoroutine
import kotlin.coroutines.resume

@RequiresApi(Build.VERSION_CODES.Q)
@Composable
fun Camera2(
  renderingEngines: List<RenderingEngine>,
  lensFacing: Int,
  context: Context = LocalContext.current,
  coroutineScope: CoroutineScope = rememberCoroutineScope()
) {
  val pixelFormat = ImageFormat.PRIVATE

  val cameraThread = HandlerThread("CameraThread").apply { start() }
  val cameraHandler = Handler(cameraThread.looper)

  val cameraManager: CameraManager by lazy {
    context.applicationContext.getSystemService(Context.CAMERA_SERVICE) as CameraManager
  }

  var currentCameraDevice: CameraDevice? by remember {
    mutableStateOf(null)
  }
  var currentImageReader: ImageReader? by remember {
    mutableStateOf(null)
  }
  var currentCameraCaptureSession: CameraCaptureSession? by remember {
    mutableStateOf(null)
  }

  fun closeCamera() {
    Log.i(TAG, "Camera2 closed, lens = $lensFacing")
    currentImageReader?.close()
    currentCameraCaptureSession?.stopRepeating()
    currentCameraCaptureSession?.close()
    currentCameraDevice?.close()
  }

  LifecycleStartEffect(lensFacing) {
    val job = coroutineScope.launch {
      val (cameraDevice, cameraCharacteristics) =
        openCamera(cameraManager, lensFacing, cameraHandler)
      currentCameraDevice = cameraDevice
      val size = cameraCharacteristics.get(
        CameraCharacteristics.SCALER_STREAM_CONFIGURATION_MAP
      )!!
        .getOutputSizes(pixelFormat).maxByOrNull { it.height * it.width }!!
      val imageReader = ImageReader.newInstance(
        size.width,
        size.height,
        pixelFormat,
        3,
        // this is crucially important as allows to render camera frame without extra copy operations
        HardwareBuffer.USAGE_GPU_SAMPLED_IMAGE
      ).also {
        currentImageReader = it
      }
      val cameraCaptureSession = suspendCancellableCoroutine {
        cameraDevice.createCaptureSession(
          listOf(imageReader.surface),
          object : StateCallback() {
            override fun onConfigured(p0: CameraCaptureSession) {
              it.resume(p0)
            }

            override fun onConfigureFailed(p0: CameraCaptureSession) {
              it.cancel(RuntimeException("createCaptureSession: onConfigureFailed"))
            }
          },
          cameraHandler
        )
      }.also {
        currentCameraCaptureSession = it
      }
      cameraCaptureSession.setRepeatingRequest(
        cameraDevice
          .createCaptureRequest(CameraDevice.TEMPLATE_PREVIEW).apply {
            addTarget(imageReader.surface)
          }
          .build(),
        null,
        cameraHandler
      )
      val rotationDegrees =
        cameraCharacteristics.get(CameraCharacteristics.SENSOR_ORIENTATION)!!
      imageReader.setOnImageAvailableListener(
        {
          val image = it.acquireLatestImage()
          image.hardwareBuffer?.let { buffer ->
            renderingEngines.forEach { engine ->
              engine.sendCameraFrame(
                buffer = buffer,
                rotationDegrees = rotationDegrees,
                backCamera = lensFacing == CameraSelector.LENS_FACING_BACK
              )
            }
            buffer.close()
          }
          image.close()
        },
        cameraHandler
      )
    }

    onStopOrDispose {
      if (job.isActive) {
        job.cancel()
      }
      closeCamera()
    }
  }
}

@SuppressLint("MissingPermission")
private suspend fun openCamera(
  cameraManager: CameraManager,
  lensFacing: Int,
  handler: Handler,
): Pair<CameraDevice, CameraCharacteristics> {
  cameraManager.cameraIdList.forEach { cameraId ->
    val characteristics = cameraManager.getCameraCharacteristics(cameraId)
    if (characteristics.get(CameraCharacteristics.LENS_FACING) == lensFacing) {
      return suspendCancellableCoroutine {
        cameraManager.openCamera(cameraId, object : CameraDevice.StateCallback() {
          override fun onOpened(device: CameraDevice) =
            it.resume(device to characteristics).also {
              Log.i(TAG, "Camera2 opened, lens = $lensFacing")
            }

          override fun onDisconnected(device: CameraDevice) {
            it.cancel(RuntimeException("Camera $cameraId has been disconnected"))
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
            it.cancel(RuntimeException("Camera $cameraId error: ($error) $msg"))
          }
        }, handler)
      }
    }
  }
  throw RuntimeException("Camera with $lensFacing was not found on device")
}