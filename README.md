# Android camera preview using [Hardware Buffers](https://developer.android.com/reference/android/hardware/HardwareBuffer).

This repository showcases how to grab camera image stream and display the preview on the [SurfaceView](https://developer.android.com/reference/android/view/SurfaceView). Almost all the logic is written in C++.

<p align="center">
  <img src="https://github.com/user-attachments/assets/22061ca9-40e2-4783-a57a-1f1b52f77d9b" alt="showcase" width="200"/>
</p>

## Instructions and requirements

Requires Android SDK >= 26 and NDK 25.1.8937393.
Open the project in Android Studio, make sure NDK is installed and run.

## Overview and technology stack
- Using [NDK Native Hardware Buffer](https://developer.android.com/ndk/reference/group/a-hardware-buffer) along with EGL and Vulkan extensions to work with HW buffers and convert them to an OpenGL ES external texture or Vulkan image backed by external memory.
- Supporting both OpenGL ES 3 **and** Vulkan 1.3 rendering backends for [Android CameraX](https://developer.android.com/training/camerax).
  - Noting that CameraX hardware buffer is provided with `AHARDWAREBUFFER_USAGE_CPU*` flags so I have to re-allocate buffers internally so that they could be used as Vulkan external memory.
- Supporting OpenGL ES 3 rendering backend for [Android Camera2](https://developer.android.com/media/camera/camera2).
  - Noting `ImageReader` is configured to generate images with `AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE` flag so **no extra copy operations** happen, hardware buffer is mapped directly to [OpenGL external texture](https://registry.khronos.org/OpenGL/extensions/OES/OES_EGL_image_external.txt).
  - There is no Vulkan support as actual underlying GPU sampled image has YUV format and Vulkan extension adaptation is required (use YUV [sampler](https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VK_KHR_sampler_ycbcr_conversion.html)).
- Using [Jetpack Compose](https://developer.android.com/courses/jetpack-compose/course) to build comprehensive UI.
- [Android CameraX](https://developer.android.com/training/camerax) is used to configure Android camera. Pretty interesting remark is that we are not even binding preview use case and make use only of [image analysis](https://developer.android.com/training/camerax/analyze).
- Using dedicated background thread to obtain camera images represented as [ImageProxy](https://developer.android.com/reference/androidx/camera/core/ImageProxy).
- Using dedicated render thread in C++ backed up by [NDK Looper](https://developer.android.com/ndk/reference/group/looper).
- Using [NDK Choreographer](https://developer.android.com/ndk/reference/group/choreographer) for effective rendering.

## Next steps / tasks
- Investigate CameraX to provide [Hardware Buffers](https://developer.android.com/reference/android/hardware/HardwareBuffer) with `AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE` usage flag.
- Add support to take a photo by using [AHardwareBuffer_lock](https://developer.android.com/ndk/reference/group/a-hardware-buffer#ahardwarebuffer_lock) functionality and compare it with [glReadPixels](https://www.khronos.org/registry/OpenGL-Refpages/es3.0/html/glReadPixels.xhtml) approach.
- Gather some metrics to check [Hardware Buffers](https://developer.android.com/reference/android/hardware/HardwareBuffer) performance in comparison with more classic approaches.
- Add support for `ImageAnalysis.Builder().setOutputImageFormat(ImageAnalysis.OUTPUT_IMAGE_FORMAT_YUV_420_888)`.

