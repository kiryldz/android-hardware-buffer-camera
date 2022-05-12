#pragma once

#include <android/looper.h>

#include <fcntl.h>
#include <thread>
#include <unistd.h>
#include <stdint.h>

#include <tbb/concurrent_hash_map.h>

#include "util.hpp"

namespace engine {
namespace android {

#define PIPE_OUT 0
#define PIPE_IN  1

/**
 * Convenient class representing thread with ALooper attached.
 */
class LooperThread {
public:
  LooperThread();
  ~LooperThread();

  void scheduleTask(const std::function<void()>& function);

private:
  std::thread thread;
  ALooper * aLooper = nullptr;

  int fds[2];
  /**
   * Using thread-safe hash map as record could be inserted from any thread.
   */
  tbb::concurrent_hash_map<uintptr_t, std::function<void()>> methodMap;

  void eventLoop();
  static int looperCallback(int fd, int events, void* data);
};

} // namespace android
} // namespace engine