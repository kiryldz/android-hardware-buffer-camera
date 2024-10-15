package com.dz.camerafast

import android.annotation.SuppressLint
import android.view.SurfaceView
import androidx.compose.runtime.Composable
import androidx.compose.ui.Modifier
import androidx.compose.ui.viewinterop.AndroidView

@SuppressLint("UnsafeOptInUsageError")
@Composable
fun CameraPreviewView(
  coreEngine: CoreEngine,
  modifier: Modifier = Modifier
) {
  AndroidView(
    modifier = modifier,
    factory = { context ->
      SurfaceView(context).apply { coreEngine.surfaceHolder = this.holder }
    },
    update = {
      // could not be used efficiently as this will always be called from main thread
      // while we desire to send image data to core with camera thread
    }
  )
}