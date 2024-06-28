#include "core_engine.hpp"

namespace engine {
namespace android {

CoreEngine::CoreEngine(JNIEnv &env, jni::jint renderingMode) : aNativeWindow(nullptr) {
    switch (renderingMode) {
        case 0: {
            LOGI("Using OpenGL ES renderer");
            renderer = std::make_unique<OpenGLRenderer>();
            break;
        }
        case 1: {
            LOGI("Using Vulkan renderer");
            break;
        }
        default: {
            throw std::exception();
        }
    }
}

CoreEngine::~CoreEngine() = default;

/** called from Android main thread **/
void CoreEngine::nativeSetSurface(JNIEnv &env, const jni::Object <Surface> &surface,
                                  jni::jint width, jni::jint height) {
    if (surface.get() != nullptr) {
        auto *nativeWindow = ANativeWindow_fromSurface(&env, jni::Unwrap(*surface.get()));
        if (nativeWindow != aNativeWindow) {
            aNativeWindow = nativeWindow;
            ANativeWindow_acquire(aNativeWindow);
            renderer->setWindow(nativeWindow);
        }
        if (nativeWindow) {
            renderer->updateWindowSize(width, height);
        }
    } else {
        renderer->resetWindow();
        ANativeWindow_release(aNativeWindow);
        aNativeWindow = nullptr;
    }
}

/** called from worker thread **/
void CoreEngine::nativeFeedHardwareBuffer(JNIEnv &env,
                                          const jni::Object <HardwareBuffer> &buffer) {
    renderer->feedHardwareBuffer(
            AHardwareBuffer_fromHardwareBuffer(&env, jni::Unwrap(*buffer.get())));
}

void CoreEngine::nativeDestroy(JNIEnv &env) {
    LOGI("Core engine destroy started");
    renderer.reset();
    LOGI("Core engine destroy passed");
}

} // namespace android
} // namespace engine