package com.dz.camerafast

import android.os.Build
import androidx.annotation.RequiresApi
import androidx.camera.core.CameraSelector
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material.FloatingActionButton
import androidx.compose.material.Icon
import androidx.compose.material.Text
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.PlayArrow
import androidx.compose.material.icons.filled.Refresh
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.dz.camerafast.camera.CameraData

@RequiresApi(Build.VERSION_CODES.Q)
@Composable
fun CameraBox(
  modifier: Modifier = Modifier,
  cameraData: CameraData,
  renderingEngine: RenderingEngine,
  onCameraChanged: (CameraMode) -> Unit,
  onLensOrientationChanged: (Int) -> Unit,
) {
  Box(modifier = modifier) {
    CameraPreviewView(
      modifier = modifier,
      renderingEngine = renderingEngine
    )
    Text(
      text = "${cameraData.cameraMode.name} | ${renderingEngine.mode.name}",
      color = Color.White,
      textAlign = TextAlign.Center,
      fontSize = 30.sp,
      modifier = Modifier
        .background(Color.DarkGray)
        .fillMaxWidth()
        .padding(10.dp)
    )
    // Camera2 with needed config is not supported for all devices
    if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
      FloatingActionButton(
        modifier = Modifier.align(Alignment.BottomEnd),
        onClick = {
          when (cameraData.cameraMode) {
            CameraMode.CAMERA_X -> onCameraChanged.invoke(CameraMode.CAMERA_2)
            CameraMode.CAMERA_2 -> onCameraChanged.invoke(CameraMode.CAMERA_X)
            CameraMode.NONE -> throw UnsupportedOperationException()
          }
        }
      ) {
        Icon(
          imageVector = Icons.Default.PlayArrow,
          contentDescription = "Switch camera"
        )
      }
    }
    FloatingActionButton(
      modifier = Modifier.align(Alignment.BottomStart),
      onClick = {
        if (cameraData.lensOrientation == CameraSelector.LENS_FACING_BACK) {
          onLensOrientationChanged(CameraSelector.LENS_FACING_FRONT)
        } else if (cameraData.lensOrientation == CameraSelector.LENS_FACING_FRONT) {
          onLensOrientationChanged(CameraSelector.LENS_FACING_BACK)
        }
      }
    ) {
      Icon(
        imageVector = Icons.Default.Refresh,
        contentDescription = "Switch camera lens"
      )
    }
  }
}