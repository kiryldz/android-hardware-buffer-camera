package com.dz.camerafast

import android.Manifest
import android.annotation.SuppressLint
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.camera.core.CameraSelector
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.core.content.PermissionChecker
import com.dz.camerafast.camera.Camera2
import com.dz.camerafast.camera.CameraData
import com.dz.camerafast.camera.CameraX

class CameraActivity : ComponentActivity() {

  private val vulkanRenderingEngine = RenderingEngine(RenderingMode.VULKAN)
  private val openGlRenderingEngine = RenderingEngine(RenderingMode.OPEN_GL_ES)
  private var initialCameraMode = CameraMode.NONE

  private val requestPermissionLauncher = registerForActivityResult(
    ActivityResultContracts.RequestPermission()
  ) { isGranted ->
    if (isGranted) {
      initialCameraMode = CameraMode.CAMERA_X
    } else {
      finish()
    }
  }

  @SuppressLint("NewApi")
  override fun onCreate(savedInstanceState: Bundle?) {
    super.onCreate(savedInstanceState)
    setContent {
      var vulkanCameraData by remember {
        mutableStateOf(CameraData(initialCameraMode, CameraSelector.LENS_FACING_FRONT))
      }

      var openGlCameraData by remember {
        mutableStateOf(CameraData(initialCameraMode, CameraSelector.LENS_FACING_FRONT))
      }

      // When an engine moves onto the source the other engine is already on, adopt
      // the other engine's lens so both states agree before the camera opens.
      fun moveTo(self: CameraData, other: CameraData, target: CameraMode) = CameraData(
        cameraMode = target,
        lensOrientation = if (other.cameraMode == target) other.lensOrientation else self.lensOrientation
      )

      if (vulkanCameraData.cameraMode != CameraMode.NONE) {
        Column {
          CameraBox(
            modifier = Modifier
              .weight(1f)
              .fillMaxWidth(),
            cameraData = vulkanCameraData,
            renderingEngine = vulkanRenderingEngine,
            onCameraChanged = { cameraMode ->
              vulkanCameraData = moveTo(vulkanCameraData, openGlCameraData, cameraMode)
            },
            onLensOrientationChanged = { lensOrientation ->
              vulkanCameraData = vulkanCameraData.copy(lensOrientation = lensOrientation)
              if (openGlCameraData.cameraMode == vulkanCameraData.cameraMode) {
                openGlCameraData = openGlCameraData.copy(lensOrientation = lensOrientation)
              }
            }
          )
          CameraBox(
            modifier = Modifier
              .weight(1f)
              .fillMaxWidth(),
            cameraData = openGlCameraData,
            renderingEngine = openGlRenderingEngine,
            onCameraChanged = { cameraMode ->
              openGlCameraData = moveTo(openGlCameraData, vulkanCameraData, cameraMode)
            },
            onLensOrientationChanged = { lensOrientation ->
              openGlCameraData = openGlCameraData.copy(lensOrientation = lensOrientation)
              if (openGlCameraData.cameraMode == vulkanCameraData.cameraMode) {
                vulkanCameraData = vulkanCameraData.copy(lensOrientation = lensOrientation)
              }
            }
          )
        }

        val cameraXEngines = buildList {
          if (vulkanCameraData.cameraMode == CameraMode.CAMERA_X) add(vulkanRenderingEngine)
          if (openGlCameraData.cameraMode == CameraMode.CAMERA_X) add(openGlRenderingEngine)
        }
        if (cameraXEngines.isNotEmpty()) {
          CameraX(
            renderingEngines = cameraXEngines,
            lensFacing = if (vulkanCameraData.cameraMode == CameraMode.CAMERA_X) {
              vulkanCameraData.lensOrientation
            } else {
              openGlCameraData.lensOrientation
            }
          )
        }

        val camera2Engines = buildList {
          if (vulkanCameraData.cameraMode == CameraMode.CAMERA_2) add(vulkanRenderingEngine)
          if (openGlCameraData.cameraMode == CameraMode.CAMERA_2) add(openGlRenderingEngine)
        }
        if (camera2Engines.isNotEmpty()) {
          Camera2(
            renderingEngines = camera2Engines,
            lensFacing = if (vulkanCameraData.cameraMode == CameraMode.CAMERA_2) {
              vulkanCameraData.lensOrientation
            } else {
              openGlCameraData.lensOrientation
            }
          )
        }
      }
    }
  }

  override fun onResume() {
    super.onResume()
    if (PermissionChecker.checkSelfPermission(
        this,
        Manifest.permission.CAMERA
      ) != PermissionChecker.PERMISSION_GRANTED
    ) {
      requestPermissionLauncher.launch(Manifest.permission.CAMERA)
    } else {
      initialCameraMode = CameraMode.CAMERA_X
    }
  }

  override fun onDestroy() {
    super.onDestroy()
    openGlRenderingEngine.destroy()
    vulkanRenderingEngine.destroy()
  }

  internal companion object {
    internal const val TAG = "DzCamera"
  }
}