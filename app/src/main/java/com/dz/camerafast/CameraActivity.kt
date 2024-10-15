package com.dz.camerafast

import android.Manifest
import android.annotation.SuppressLint
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.camera.core.CameraSelector
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.tween
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableStateListOf
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
      val enginesCamera2 = remember {
        mutableStateListOf(vulkanRenderingEngine, openGlRenderingEngine)
      }

      val enginesCameraX = remember {
        mutableStateListOf(vulkanRenderingEngine, openGlRenderingEngine)
      }

      var vulkanCameraData by remember {
        mutableStateOf(CameraData(initialCameraMode, CameraSelector.LENS_FACING_FRONT))
      }

      var openGlCameraData by remember {
        mutableStateOf(CameraData(initialCameraMode, CameraSelector.LENS_FACING_FRONT))
      }

      var vulkanWeightValue by remember {
        mutableFloatStateOf(1.0f)
      }

      val vulkanWeight by animateFloatAsState(
        targetValue = vulkanWeightValue,
        // TODO increase duration and try make dynamic view resize less laggy
        animationSpec = tween(durationMillis = 0),
        label = "Vulkan",
      )

      var openGlWeightValue by remember {
        mutableFloatStateOf(1.0f)
      }

      val openGlWeight by animateFloatAsState(
        targetValue = openGlWeightValue,
        // TODO increase duration and try make dynamic view resize less laggy
        animationSpec = tween(durationMillis = 0),
        label = "OpenGL",
      )

      if (vulkanCameraData.cameraMode != CameraMode.NONE) {
        Column {
          CameraBox(
            modifier = Modifier
              .weight(vulkanWeight)
              .fillMaxWidth(),
            cameraData = vulkanCameraData,
            renderingEngine = vulkanRenderingEngine,
            onCameraChanged = { cameraMode ->
              vulkanCameraData = CameraData(
                cameraMode = when (cameraMode) {
                  CameraMode.CAMERA_X -> {
                    enginesCamera2.remove(vulkanRenderingEngine)
                    if (enginesCameraX.contains(vulkanRenderingEngine).not()) {
                      enginesCameraX.add(vulkanRenderingEngine)
                    }
                    CameraMode.CAMERA_X
                  }

                  CameraMode.CAMERA_2 -> {
                    enginesCameraX.remove(vulkanRenderingEngine)
                    if (enginesCamera2.contains(vulkanRenderingEngine).not()) {
                      enginesCamera2.add(vulkanRenderingEngine)
                    }
                    CameraMode.CAMERA_2
                  }

                  CameraMode.NONE -> throw UnsupportedOperationException()
                },
                lensOrientation = vulkanCameraData.lensOrientation
              )
            },
            onLensOrientationChanged = { lensOrientation ->
              vulkanCameraData = CameraData(
                cameraMode = vulkanCameraData.cameraMode,
                lensOrientation = when (lensOrientation) {
                  CameraSelector.LENS_FACING_BACK -> CameraSelector.LENS_FACING_BACK
                  CameraSelector.LENS_FACING_FRONT -> CameraSelector.LENS_FACING_FRONT
                  else -> throw UnsupportedOperationException()
                }
              )
            }
          )
          CameraBox(
            modifier = Modifier
              .weight(openGlWeight)
              .fillMaxWidth(),
            cameraData = openGlCameraData,
            renderingEngine = openGlRenderingEngine,
            onCameraChanged = { cameraMode ->
              openGlCameraData = CameraData(
                cameraMode = when (cameraMode) {
                  CameraMode.CAMERA_X -> {
                    enginesCamera2.remove(openGlRenderingEngine)
                    if (enginesCameraX.contains(openGlRenderingEngine).not()) {
                      enginesCameraX.add(openGlRenderingEngine)
                    }
                    CameraMode.CAMERA_X
                  }

                  CameraMode.CAMERA_2 -> {
                    enginesCameraX.remove(openGlRenderingEngine)
                    if (enginesCamera2.contains(openGlRenderingEngine).not()) {
                      enginesCamera2.add(openGlRenderingEngine)
                    }
                    CameraMode.CAMERA_2
                  }

                  CameraMode.NONE -> throw UnsupportedOperationException()
                },
                lensOrientation = openGlCameraData.lensOrientation
              )
            },
            onLensOrientationChanged = { lensOrientation ->
              openGlCameraData = CameraData(
                cameraMode = openGlCameraData.cameraMode,
                lensOrientation = when (lensOrientation) {
                  CameraSelector.LENS_FACING_BACK -> CameraSelector.LENS_FACING_BACK
                  CameraSelector.LENS_FACING_FRONT -> CameraSelector.LENS_FACING_FRONT
                  else -> throw UnsupportedOperationException()
                }
              )
            }
          )
        }
        if (vulkanCameraData.cameraMode == CameraMode.CAMERA_X || openGlCameraData.cameraMode == CameraMode.CAMERA_X) {
          CameraX(
            renderingEngines = enginesCameraX,
            lensFacing = if (vulkanCameraData.cameraMode == CameraMode.CAMERA_X) {
              vulkanCameraData.lensOrientation
            } else if (openGlCameraData.cameraMode == CameraMode.CAMERA_X) {
              openGlCameraData.lensOrientation
            } else {
              throw UnsupportedOperationException()
            }
          )
        }

        if (vulkanCameraData.cameraMode == CameraMode.CAMERA_2 || openGlCameraData.cameraMode == CameraMode.CAMERA_2) {
          Camera2(
            renderingEngines = enginesCamera2,
            lensFacing = if (vulkanCameraData.cameraMode == CameraMode.CAMERA_2) {
              vulkanCameraData.lensOrientation
            } else if (openGlCameraData.cameraMode == CameraMode.CAMERA_2) {
              openGlCameraData.lensOrientation
            } else {
              throw UnsupportedOperationException()
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