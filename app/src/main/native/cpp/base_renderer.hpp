#pragma once

#include <android/choreographer.h>
#include <android/hardware_buffer.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>

#include <queue>

#include "looper_thread.hpp"
#include "util.hpp"

namespace engine {
namespace android {

class BaseRenderer {

public:
    BaseRenderer();

    ~BaseRenderer();

    void setWindow(ANativeWindow *window);

    void updateWindowSize(int width, int height);

    void resetWindow();

    /**
     * Always called from camera worker thread - feed new camera buffer.
     * @param aHardwareBuffer
     */
    void feedHardwareBuffer(AHardwareBuffer *aHardwareBuffer);

protected:
    virtual const char *renderingModeName() = 0;

    virtual bool onWindowCreated() = 0;

    virtual void onWindowDestroyed() = 0;

    virtual void onWindowSizeUpdated(int width, int height) = 0;

    virtual void hwBufferToTexture(AHardwareBuffer *buffer) = 0;

    virtual bool couldRender() const = 0;

    virtual void render() = 0;

    // TODO need another function as real function is static and could not be moved to base class,
    //  perhaps could be done better
    virtual void postChoreographerCallback() = 0;

    ANativeWindow *aNativeWindow = nullptr;
    AChoreographer *aChoreographer = nullptr;

    int viewportWidth = -1;
    int viewportHeight = -1;
    volatile bool hardwareBufferDescribed = false;
    float bufferImageRatio = 1.0f;

    /**
     * Queue with a mutex needed as worker camera thread produces buffers while render thread consumes them.
     */
    std::queue<AHardwareBuffer *> aHwBufferQueue;
    std::mutex bufferQueueMutex;

private:
    std::unique_ptr <LooperThread> renderThread;
    std::mutex mutex;
    std::condition_variable initCondition;
    std::condition_variable destroyCondition;
};

} // namespace android
} // namespace engine