package com.dz.camerafast.camera

import android.content.Context
import android.util.Log
import androidx.annotation.OptIn
import androidx.camera.core.CameraSelector
import androidx.camera.core.ExperimentalGetImage
import androidx.camera.core.ImageAnalysis
import androidx.camera.core.resolutionselector.AspectRatioStrategy
import androidx.camera.core.resolutionselector.ResolutionSelector
import androidx.camera.lifecycle.ProcessCameraProvider
import androidx.compose.runtime.Composable
import androidx.compose.ui.platform.LocalContext
import androidx.core.content.ContextCompat
import androidx.lifecycle.LifecycleOwner
import androidx.lifecycle.compose.LifecycleStartEffect
import com.dz.camerafast.CameraActivity.Companion.TAG
import com.dz.camerafast.RenderingEngine
import java.util.concurrent.Executors

@OptIn(ExperimentalGetImage::class)
@Composable
fun CameraX(
  renderingEngines: List<RenderingEngine>,
  lensFacing: Int,
  context: Context = LocalContext.current
) {
  LifecycleStartEffect(lensFacing) {
    val cameraProviderFuture = ProcessCameraProvider.getInstance(context)
    cameraProviderFuture.addListener({
      // Camera provider is now guaranteed to be available
      val cameraProvider = cameraProviderFuture.get()

      // Set up the image analysis use case which will process frames in real time using dedicated thread
      val imageAnalysis = ImageAnalysis.Builder()
        .setResolutionSelector(
          ResolutionSelector.Builder()
            .setAspectRatioStrategy(AspectRatioStrategy.RATIO_16_9_FALLBACK_AUTO_STRATEGY)
            .build()
        )
        .setOutputImageFormat(ImageAnalysis.OUTPUT_IMAGE_FORMAT_RGBA_8888)
        .setBackpressureStrategy(ImageAnalysis.STRATEGY_KEEP_ONLY_LATEST)
        .build()

      imageAnalysis.setAnalyzer(Executors.newSingleThreadExecutor()) { imageProxy ->
        Log.i(TAG, "New image ${imageProxy.hashCode()} arrived!")
        imageProxy.image?.hardwareBuffer?.let { buffer ->
          renderingEngines.forEach {
            it.sendCameraFrame(
              buffer = buffer,
              rotationDegrees = imageProxy.imageInfo.rotationDegrees,
              backCamera = lensFacing == CameraSelector.LENS_FACING_BACK
            )
          }
          buffer.close()
        }
        imageProxy.close()
        Log.i(TAG, "Image ${imageProxy.hashCode()} and buffer are closed!")
      }

      val cameraSelector = CameraSelector.Builder()
        .requireLensFacing(lensFacing)
        .build()

      // Apply declared configs to CameraX using the same lifecycle owner
      cameraProvider.unbindAll()
      // Note: we do not setup ANY preview options as all the drawing will be done by us
      cameraProvider.bindToLifecycle(
        context as LifecycleOwner, cameraSelector, imageAnalysis
      )
      Log.i(TAG, "Camera set up!")
    }, ContextCompat.getMainExecutor(context))

    onStopOrDispose {
      cameraProviderFuture.cancel(true)
      ProcessCameraProvider.getInstance(context).get()?.unbindAll()
    }
  }
}