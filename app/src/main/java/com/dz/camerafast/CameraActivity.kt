package com.dz.camerafast

import android.Manifest
import android.annotation.SuppressLint
import android.content.Context
import android.content.pm.PackageManager
import android.hardware.HardwareBuffer
import android.os.Bundle
import android.util.Log
import android.view.SurfaceView
import androidx.appcompat.app.AppCompatActivity
import androidx.camera.core.AspectRatio
import androidx.camera.core.CameraSelector
import androidx.camera.core.ImageAnalysis
import androidx.camera.core.resolutionselector.AspectRatioStrategy
import androidx.camera.core.resolutionselector.ResolutionSelector
import androidx.camera.core.resolutionselector.ResolutionStrategy
import androidx.camera.lifecycle.ProcessCameraProvider
import androidx.core.app.ActivityCompat
import androidx.core.content.ContextCompat
import androidx.lifecycle.LifecycleOwner
import java.util.concurrent.Executors
import kotlin.experimental.and
import kotlin.random.Random

class CameraActivity : AppCompatActivity() {

    private val permissions = listOf(Manifest.permission.CAMERA)
    private val permissionsRequestCode = Random.nextInt(0, 10000)

    private lateinit var coreEngine: CoreEngine

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        setContentView(R.layout.activity_main)
        coreEngine = CoreEngine(
            surfaceHolder = findViewById<SurfaceView>(R.id.surface_view).holder,
            renderingMode = RenderingMode.OPEN_GL_ES,
        )
    }

    override fun onResume() {
        super.onResume()
        // Request permissions each time the app resumes, since they can be revoked at any time
        if (!hasPermissions(this)) {
            ActivityCompat.requestPermissions(
                this, permissions.toTypedArray(), permissionsRequestCode
            )
        } else {
            bindCameraUseCases()
        }
    }

    override fun onDestroy() {
        super.onDestroy()
        coreEngine.destroy()
    }

    override fun onRequestPermissionsResult(
        requestCode: Int,
        permissions: Array<out String>,
        grantResults: IntArray
    ) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults)
        if (requestCode == permissionsRequestCode && hasPermissions(this)) {
            bindCameraUseCases()
        } else {
            finish() // If we don't have the required permissions, we can't run
        }
    }

    @SuppressLint("UnsafeOptInUsageError")
    private fun bindCameraUseCases() {
        val cameraProviderFuture = ProcessCameraProvider.getInstance(this)
        cameraProviderFuture.addListener({

            // Camera provider is now guaranteed to be available
            val cameraProvider = cameraProviderFuture.get()

            // Set up the image analysis use case which will process frames in real time using dedicated thread

            // We step away from official guidelines and use STRATEGY_BLOCK_PRODUCER as at least
            // on Xiaomi Mi 9 STRATEGY_KEEP_ONLY_LATEST strategy proved to be non-efficient -
            // we're using concurrent HW buffer queue in core where camera worker thread feeds buffers ASAP
            // while core render thread pop them ASAP providing best balance as in some situations vendor
            // camera implementation may produce more in short period of time followed by some delay when
            // we catch up by popping the queue when we have a moment between VSYNC calls
            val imageAnalysis = ImageAnalysis.Builder()
                .setResolutionSelector(
                    ResolutionSelector.Builder()
                        .setAspectRatioStrategy(AspectRatioStrategy.RATIO_16_9_FALLBACK_AUTO_STRATEGY)
                        .build()
                )
                .setOutputImageFormat(ImageAnalysis.OUTPUT_IMAGE_FORMAT_RGBA_8888)
                .setBackpressureStrategy(ImageAnalysis.STRATEGY_BLOCK_PRODUCER)
                .setImageQueueDepth(CAMERA_IMAGE_QUEUE_DEPTH)
                .build()

            imageAnalysis.setAnalyzer(Executors.newSingleThreadExecutor()) { imageProxy ->
                Log.i(TAG, "New image arrived!")
                imageProxy.image?.hardwareBuffer?.let { buffer ->
                    buffer.printSupportedUsageFlags()
                    coreEngine.feedHardwareBuffer(buffer)
                    // from docs for `buffer.close()`:
                    // Calling this method frees up any underlying native resources.
                    //
                    // This does not seem to be true as we acquire buffer in C++ and
                    // release it on a render thread.
                    buffer.close()
                    Log.i(TAG, "Buffer closed!")
                }
                imageProxy.close()
                Log.i(TAG, "Image closed!")
            }

            // Create a new camera selector each time, enforcing lens facing
            val cameraSelector = CameraSelector.Builder()
                .requireLensFacing(CameraSelector.LENS_FACING_FRONT)
                .build()

            // Apply declared configs to CameraX using the same lifecycle owner
            cameraProvider.unbindAll()
            // Note: we do not setup ANY preview options as all the drawing will be done by us
            cameraProvider.bindToLifecycle(
                this as LifecycleOwner, cameraSelector, imageAnalysis
            )
            Log.i(TAG, "Camera set up!")
        }, ContextCompat.getMainExecutor(this))
    }

    /** Convenience method used to check if all permissions required by this app are granted */
    private fun hasPermissions(context: Context) = permissions.all {
        ContextCompat.checkSelfPermission(context, it) == PackageManager.PERMISSION_GRANTED
    }

    private fun HardwareBuffer.printSupportedUsageFlags() {
        val usage = usage.toInt()
        val supportedUsages = mutableListOf<String>()

        if (usage and HardwareBuffer.USAGE_CPU_READ_RARELY.toInt() != 0) {
            supportedUsages.add("USAGE_CPU_READ_RARELY")
        }
        if (usage and HardwareBuffer.USAGE_CPU_READ_OFTEN.toInt() != 0) {
            supportedUsages.add("USAGE_CPU_READ_OFTEN")
        }
        if (usage and HardwareBuffer.USAGE_CPU_WRITE_RARELY.toInt() != 0) {
            supportedUsages.add("USAGE_CPU_WRITE_RARELY")
        }
        if (usage and HardwareBuffer.USAGE_CPU_WRITE_OFTEN.toInt() != 0) {
            supportedUsages.add("USAGE_CPU_WRITE_OFTEN")
        }
        if (usage and HardwareBuffer.USAGE_GPU_SAMPLED_IMAGE.toInt() != 0) {
            supportedUsages.add("USAGE_GPU_SAMPLED_IMAGE")
        }
        if (usage and HardwareBuffer.USAGE_GPU_COLOR_OUTPUT.toInt() != 0) {
            supportedUsages.add("USAGE_GPU_COLOR_OUTPUT")
        }
        if (usage and HardwareBuffer.USAGE_GPU_CUBE_MAP.toInt() != 0) {
            supportedUsages.add("USAGE_GPU_CUBE_MAP")
        }
        if (usage and HardwareBuffer.USAGE_GPU_MIPMAP_COMPLETE.toInt() != 0) {
            supportedUsages.add("USAGE_GPU_MIPMAP_COMPLETE")
        }
        if (usage and HardwareBuffer.USAGE_PROTECTED_CONTENT.toInt() != 0) {
            supportedUsages.add("USAGE_PROTECTED_CONTENT")
        }
        if (usage and HardwareBuffer.USAGE_SENSOR_DIRECT_DATA.toInt() != 0) {
            supportedUsages.add("USAGE_SENSOR_DIRECT_DATA")
        }
        if (usage and HardwareBuffer.USAGE_VIDEO_ENCODE.toInt() != 0) {
            supportedUsages.add("USAGE_VIDEO_ENCODE")
        }

        println("Supports ${supportedUsages.joinToString(", ")}")
    }

    private companion object {
        private val TAG = "DzCamera"
        // Not making buffer too big as it may bring in latency, 3 seems to be pretty balanced
        private const val CAMERA_IMAGE_QUEUE_DEPTH = 3
    }
}