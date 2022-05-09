#pragma once

#include <android/looper.h>

#include <fcntl.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>

#include "util.hpp"

namespace engine {
namespace android {

#define PIPE_OUT 0
#define PIPE_IN  1
#define STOP_THREAD_TASK_ID 0

/**
 * Convenient class representing thread with ALooper attached.
 */
class LooperThread {
public:
  LooperThread();
  ~LooperThread();

  uint8_t registerTask(const std::function<void()>& task);
  void scheduleTask(uint8_t id);

private:
  std::thread thread;
  ALooper * aLooper = nullptr;

  /**
   * Leave some reserved slots for internal tasks.
   */
  uint8_t lastTakenTaskId = 5;
  int fds[2];
  std::unordered_map<uint8_t, std::function<void()>> methodMap;

  void eventLoop();
  static int looperCallback(int fd, int events, void* data);
};

} // namespace android
} // namespace engine