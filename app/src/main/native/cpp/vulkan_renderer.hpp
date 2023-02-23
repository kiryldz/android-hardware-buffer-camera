#pragma once

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_android.h>

#include "base_renderer.hpp"

namespace engine {
namespace android {

class VulkanRenderer : public BaseRenderer {

protected:
const char* renderingModeName() override {
    return (const char*) "Vulkan";
}

bool onWindowCreated() override {
    return true;
}

void onWindowSizeUpdated(int width, int height) override {

}

void onWindowDestroyed() override {

}

void hwBufferToTexture(AHardwareBuffer *buffer) override {

}

void postChoreographerCallback() override {

}

private:

};

} // namespace android
} // namespace engine
