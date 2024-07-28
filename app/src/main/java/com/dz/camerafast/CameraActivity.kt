package com.dz.camerafast

import android.Manifest
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.result.contract.ActivityResultContracts
import androidx.camera.core.CameraSelector
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.tween
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.material.Button
import androidx.compose.material.Text
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.core.content.PermissionChecker

class CameraActivity : ComponentActivity() {

    private val previewEngineList = listOf(
        CoreEngine(RenderingMode.VULKAN),
        CoreEngine(RenderingMode.OPEN_GL_ES)
    )
    private var previewCameraXState = mutableStateOf(false)

    private val requestPermissionLauncher = registerForActivityResult(
        ActivityResultContracts.RequestPermission()
    ) { isGranted ->
        if (isGranted) {
            previewCameraXState.value = true
        } else {
            finish()
        }
    }

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContent {
            val previewCameraX by remember {
                previewCameraXState
            }

            var lensFacing by remember {
                mutableIntStateOf(CameraSelector.LENS_FACING_FRONT)
            }

            var displayMode by remember {
                mutableStateOf(DisplayMode.BOTH)
            }

            var vulkanWeightValue by remember {
                mutableFloatStateOf(1.0f)
            }

            val vulkanWeight by animateFloatAsState(
                targetValue = vulkanWeightValue,
                // TODO increase duration and try make dynamic view resize less laggy
                animationSpec = tween(durationMillis = 0),
                label = "Vulkan",
                finishedListener = { endValue ->
                    displayMode = if (endValue == 1.0f) {
                        DisplayMode.BOTH
                    } else {
                        DisplayMode.OPEN_GL_ES
                    }
                }
            )

            var openGlWeightValue by remember {
                mutableFloatStateOf(1.0f)
            }

            val openGlWeight by animateFloatAsState(
                targetValue = openGlWeightValue,
                // TODO increase duration and try make dynamic view resize less laggy
                animationSpec = tween(durationMillis = 0),
                label = "OpenGL",
                finishedListener = { endValue ->
                    displayMode = if (endValue == 1.0f) {
                        DisplayMode.BOTH
                    } else {
                        DisplayMode.VULKAN
                    }
                }
            )

            if (previewCameraX) {
//                CameraX(
//                    coreEngines = previewEngineList,
//                    lensFacing = lensFacing
//                )
                Camera2(
                    coreEngines = previewEngineList,
                    lensFacing = lensFacing
                )
            }

            Column {
                if (displayMode != DisplayMode.VULKAN) {
                    Box(
                        contentAlignment = Alignment.TopStart,
                        modifier = Modifier
                            .weight(openGlWeight)
                            .fillMaxWidth()
                    ) {
                        CameraPreviewView(
                            coreEngine = previewEngineList.find { it.renderingMode == RenderingMode.OPEN_GL_ES }!!,
                            modifier = Modifier
                                .matchParentSize()
                                .clickable(enabled = displayMode == DisplayMode.BOTH) {
                                    vulkanWeightValue = 0.1f
                                }
                        )
                        // TODO world mystery - this text is not shown
                        if (displayMode == DisplayMode.BOTH) {
                            Text(
                                text = "OpenGL",
                                fontSize = 30.sp,
                                color = Color.Black,
                                modifier = Modifier.padding(8.dp)
                            )
                        }
                    }
                }
                if (displayMode != DisplayMode.OPEN_GL_ES) {
                    Box(
                        contentAlignment = Alignment.TopStart,
                        modifier = Modifier
                            .weight(vulkanWeight)
                            .fillMaxWidth()
                    ) {
                        CameraPreviewView(
                            coreEngine = previewEngineList.find { it.renderingMode == RenderingMode.VULKAN }!!,
                            modifier = Modifier
                                .matchParentSize()
                                .clickable(enabled = displayMode == DisplayMode.BOTH) {
                                    openGlWeightValue = 0.1f
                                }
                        )
                        if (displayMode == DisplayMode.BOTH) {
                            Text(
                                text = "Vulkan",
                                fontSize = 30.sp,
                                color = Color.White,
                                modifier = Modifier.padding(8.dp)
                            )
                        }
                    }
                }
                Row {
                    Button(
                        onClick = {
                            lensFacing = if (lensFacing == CameraSelector.LENS_FACING_FRONT) {
                                CameraSelector.LENS_FACING_BACK
                            } else {
                                CameraSelector.LENS_FACING_FRONT
                            }
                        },
                        modifier = Modifier
                            .padding(8.dp)
                            .weight(1.0f)
                    ) {
                        Text(text = "Switch camera")
                    }
                    if (displayMode != DisplayMode.BOTH) {
                        Button(
                            onClick = {
                                displayMode = DisplayMode.BOTH
                                vulkanWeightValue = 1.0f
                                openGlWeightValue = 1.0f
                            },
                            modifier = Modifier
                                .padding(8.dp)
                                .weight(1.0f)
                        ) {
                            Text(text = "Back to both")
                        }
                    }
                }
            }
        }
    }

    override fun onStart() {
        super.onStart()
        if (PermissionChecker.checkSelfPermission(
                this,
                Manifest.permission.CAMERA
            ) != PermissionChecker.PERMISSION_GRANTED
        ) {
            requestPermissionLauncher.launch(Manifest.permission.CAMERA)
        } else {
            previewCameraXState.value = true
        }
    }

    override fun onStop() {
        super.onStop()
        previewCameraXState.value = false
    }

    override fun onDestroy() {
        super.onDestroy()
        previewEngineList.forEach { it.destroy() }
    }

    internal companion object {
        internal const val TAG = "DzCamera"
    }
}