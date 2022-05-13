#include "looper_thread.hpp"

namespace engine {
namespace android {

LooperThread::LooperThread(): thread(std::thread(&LooperThread::eventLoop, this)) {
  LOGI("LooperThread constructor");
}

LooperThread::~LooperThread() {
  LOGI("LooperThread destructor, sent destroy task, waiting...");
  scheduleTask([this] {
    ALooper_removeFd(aLooper, fds[PIPE_OUT]);
    if (close(fds[PIPE_IN]) || close(fds[PIPE_OUT])) {
      throw std::runtime_error("Failed to close file descriptor!");
    }
    // explicit wake here in order to unblock ALooper_pollAll
    ALooper_wake(aLooper);
    ALooper_release(aLooper);
    LOGI("Looper is released!");
  });
  thread.join();
  LOGI("LooperThread destructor, thread killed!");
  taskMap.clear();
}

void LooperThread::eventLoop() {
  // in order to use AChoreographer for effective rendering
  // and allow scheduling events in general we prepare and acquire the ALooper
  aLooper = ALooper_prepare(0);
  assert(aLooper);
  ALooper_acquire(aLooper);
  if (pipe2(fds, O_CLOEXEC)) {
    throw std::runtime_error("Failed to create pipe needed for the ALooper");
  }
  // TODO understand if this is really required
  if (fcntl(fds[PIPE_OUT], F_SETFL, O_NONBLOCK)) {
    throw std::runtime_error("Failed to set pipe read end non-blocking.");
  }
  auto ret = ALooper_addFd(aLooper, fds[PIPE_OUT], ALOOPER_POLL_CALLBACK,
                           ALOOPER_EVENT_INPUT, looperCallback, this);
  if (ret != 1) {
    throw std::runtime_error("Failed to add file descriptor to Looper.");
  }

  int outFd, outEvents;
  char *outData = nullptr;

  // using negative timeout and block poll forever
  // we will be using poll callbacks to schedule events and not make use of ALooper_wake() at all unless we want to exit the thread
  ALooper_pollAll(-1, &outFd, &outEvents, reinterpret_cast<void**>(&outData));
}

int LooperThread::looperCallback(int fd, int events, void * data) {
  if (events > 1) {
    LOGW("Handling more than one event is not implemented and "
         "should not happen in current implementation. Only one event will be processed.");
  }
  uintptr_t taskId;
  while (read(fd, &taskId, sizeof(taskId)) > 0) {}
  auto * looperThread = reinterpret_cast<LooperThread*>(data);
  Accessor ac;
  if (looperThread->taskMap.find(ac, taskId)) {
    ac->second();
    looperThread->taskMap.erase(ac);
  }
  ac.release();
  return 1;
}

void LooperThread::scheduleTask(const Task& task) {
  auto key = reinterpret_cast<uintptr_t>(&task);
  Accessor ac;
  taskMap.insert(ac, key);
  ac->second = task;
  ac.release();
  write(fds[PIPE_IN], &key, sizeof(uintptr_t));
}

} // namespace android
} // namespace engine
