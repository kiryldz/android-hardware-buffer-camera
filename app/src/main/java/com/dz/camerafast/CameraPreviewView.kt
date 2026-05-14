package com.dz.camerafast

import android.annotation.SuppressLint
import android.view.TextureView
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
      TextureView(context).apply { coreEngine.textureView = this }
    },
    update = {
      // could not be used efficiently as this will always be called from main thread
      // while we desire to send image data to core with camera thread
    }
  )
}