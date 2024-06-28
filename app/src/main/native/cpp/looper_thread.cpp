#include "looper_thread.hpp"

#include <android/looper.h>

#include "run_loop.hpp"
#include "util.hpp"

// STL
#include <future>

namespace engine {
namespace android {

LooperThread::LooperThread() {
  std::promise<void> runLoopCreated;
  std::future<void> runLoopCreatedFuture = runLoopCreated.get_future();
  thread_ = std::thread([&]() {
    runLoop_ = std::make_shared<RunLoop>(ALooper_prepare(0));
    runLoopCreated.set_value();
    runLoop_->run();
  });
  runLoopCreatedFuture.wait();
  LOGI("RunLoop created and LooperThread is ready");
}

LooperThread::~LooperThread() {
  runLoop_->stop();
  thread_.join();
}

void LooperThread::scheduleTask(Task &&task) {
  runLoop_->schedule(std::make_unique<Task>(task));
}

}  // namespace android
}  // namespace engine
