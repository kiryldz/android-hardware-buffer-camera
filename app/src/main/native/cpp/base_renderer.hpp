#pragma once

#include <android/choreographer.h>
#include <android/hardware_buffer.h>
#include <android/native_window.h>
#include <android/native_window_jni.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "glm/gtx/string_cast.hpp"

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

    /**
     * Re-fit the renderer's output to the current viewport size at a UI-driven key frame
     * (threshold crossings, endpoint settlement). The filtering of which moments count as "key"
     * lives on the Kotlin side, so every call into this method is expected to be meaningful.
     *
     * Drives the renderer's onRefit() hook on the render thread — Vulkan rebuilds its swapchain
     * there; OpenGL doesn't override it because its per-tick glViewport in onWindowSizeUpdated
     * already keeps the rasterization area in sync.
     *
     * Note: onWindowSizeUpdated is the SEPARATE per-tick hook that fires on every
     * updateWindowSize task — this method does not drive that one.
     */
    void refit();

    void resetWindow();

    /**
     * Always called from camera worker thread - feed new camera buffer.
     * @param aHardwareBuffer
     */
    void processCameraFrame(AHardwareBuffer *aHardwareBuffer, int rotationDegrees_, bool backCamera_);

protected:
    virtual const char *renderingModeName() = 0;

    virtual bool onWindowCreated() = 0;

    virtual void onWindowDestroyed() = 0;

    /**
     * Lightweight size update. Called from each scheduled updateWindowSize task on the render
     * thread, with the LATEST observed dimensions. Note that updateWindowSize coalesces — a burst
     * of surfaceTextureSizeChanged callbacks collapses into a single task that sees only the most
     * recent (width, height), so intermediate sizes are skipped by design. Cheap operations
     * belong here (e.g. OpenGL's glViewport); expensive ones (e.g. Vulkan swapchain rebuild)
     * should leave this empty and react in onRefit instead.
     */
    virtual void onWindowSizeUpdated(int width, int height) = 0;

    /**
     * Heavy refit hook, fired only at UI-driven key frames (see refit()). Default is no-op;
     * Vulkan overrides to rebuild the swapchain at the current viewport size.
     */
    virtual void onRefit(int width, int height) {}

    virtual void hwBufferToTexture(AHardwareBuffer *buffer) = 0;

    virtual void onMvpUpdated() { };

    virtual bool couldRender() const = 0;

    virtual void render() = 0;

    // TODO need another function as real function is static and could not be moved to base class,
    //  perhaps could be done better
    virtual void postChoreographerCallback() = 0;

    ANativeWindow *aNativeWindow = nullptr;
    AChoreographer *aChoreographer = nullptr;

    int viewportWidth = -1;
    int viewportHeight = -1;
    glm::mat4 mvp;


    /**
     * The mutex needed as worker camera thread produces buffers while render thread consumes them.
     */
    std::mutex bufferMutex;

private:

    /**
     * Must be called from render thread only to avoid race conditions.
     */
    void updateMvp();

    /**
     * Schedule the render-thread consumer task that drains the latest pendingViewport* into
     * the applied viewportWidth/Height. Used by both updateWindowSize (initial schedule) and
     * the consumer task itself (re-schedule when a newer pending value arrived mid-task) so the
     * consumer body always re-reads pending under resizeMutex — never with a stale snapshot,
     * and never via the producer entry that would clobber pending.
     */
    void scheduleApplyPendingViewportSize();


    float bufferImageRatio = 1.0f;
    int rotationDegrees = 0;
    bool backCamera = false;

    std::unique_ptr <LooperThread> renderThread;
    std::mutex mutex;
    std::condition_variable initCondition;
    std::condition_variable destroyCondition;

    // Coalescing state for updateWindowSize: layout animations can fire surfaceTextureSizeChanged
    // at ~60Hz, and a queued backlog of per-tick work is what would make resize feel sluggish.
    // Producers stash the latest requested size under resizeMutex and only schedule a render-thread
    // task if one is not already in flight; the task reads whichever size is current when it runs.
    std::mutex resizeMutex;
    int pendingViewportWidth = -1;
    int pendingViewportHeight = -1;
    bool resizeTaskScheduled = false;
};

} // namespace android
} // namespace engine