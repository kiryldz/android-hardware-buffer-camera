package com.dz.camerafast.camera

import androidx.compose.runtime.Immutable
import com.dz.camerafast.CameraMode

@Immutable
data class CameraData(
  val cameraMode: CameraMode,
  val lensOrientation: Int,
)