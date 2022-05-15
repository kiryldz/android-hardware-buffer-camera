#pragma once

// STL
#include <functional>
#include <memory>
#include <thread>

namespace engine {
namespace android {

using Task = std::function<void()>;
class RunLoop;

/**
 * Convenient class representing thread with ALooper attached.
 */
class LooperThread {
public:
    LooperThread();
    ~LooperThread();

    void scheduleTask(Task&& task);

private:
    std::shared_ptr<RunLoop> runLoop_;
    std::thread thread_;
};

}  // namespace android
}  // namespace engine