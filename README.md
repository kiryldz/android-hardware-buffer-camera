# Android camera preview using [Hardware Buffers](https://developer.android.com/reference/android/hardware/HardwareBuffer).

This repository showcases how to grab camera image stream and display the preview on the [SurfaceView](https://developer.android.com/reference/android/view/SurfaceView). Almost all the logic is written in C++.

## Instructions and requirements

Requires Android SDK >= 26.
Open the project in Android Studio, make sure NDK is installed and run.

## Overview and technology stack

- [Android CameraX](https://developer.android.com/training/camerax) is used to configure Android camera. Pretty interesting remark is that we are not even binding preview use case and make use only of [image analysis](https://developer.android.com/training/camerax/analyze).
- Using dedicated background thread to obtain camera images represented as [ImageProxy](https://developer.android.com/reference/androidx/camera/core/ImageProxy).
- Using dedicated render thread in C++ backed up by [NDK Looper](https://developer.android.com/ndk/reference/group/looper).
- Using [NDK Choreographer](https://developer.android.com/ndk/reference/group/choreographer) for effective rendering.
- Using [NDK Native Hardware Buffer](https://developer.android.com/ndk/reference/group/a-hardware-buffer) along with EGL extensions to work with HW buffers and convert them to OpenGL ES external texture.
- Using OpenGL ES 3 for rendering and thus a bit more modern GLSL.

## Next steps / tasks
- Investigate Android Camera to provide [Hardware Buffers](https://developer.android.com/reference/android/hardware/HardwareBuffer)s with `AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE` usage flag. For now I have to re-allocate buffers internally so that they could be used as Vulkan external memory.
- Add support to take a photo by using [AHardwareBuffer_lock](https://developer.android.com/ndk/reference/group/a-hardware-buffer#ahardwarebuffer_lock) functionality and compare it with [glReadPixels](https://www.khronos.org/registry/OpenGL-Refpages/es3.0/html/glReadPixels.xhtml) approach.
- Add support for back camera as well.
- Gather some metrics to check [Hardware Buffers](https://developer.android.com/reference/android/hardware/HardwareBuffer) performance in comparison with more classic approaches.
- Add support for `ImageAnalysis.Builder().setOutputImageFormat(ImageAnalysis.OUTPUT_IMAGE_FORMAT_YUV_420_888)`

