#pragma once

#include "base_renderer.hpp"

namespace engine {
namespace android {

class VulkanRenderer : public BaseRenderer {

public:
    VulkanRenderer();
    ~VulkanRenderer();

    void setWindow(ANativeWindow *window) override;
    void updateWindowSize(int width, int height) override;
    void resetWindow() override;
    void feedHardwareBuffer(AHardwareBuffer *aHardwareBuffer) override;
};

} // namespace android
} // namespace engine
