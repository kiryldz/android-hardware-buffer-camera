#include "looper_thread.hpp"

namespace engine {
namespace android {

LooperThread::LooperThread(): thread(std::thread(&LooperThread::eventLoop, this)) {
  LOGI("LooperThread constructor");
  // register internal destroy task by simply putting it in the hashmap
  methodMap.emplace(STOP_THREAD_TASK_ID, nullptr);
}

LooperThread::~LooperThread() {
  scheduleTask(STOP_THREAD_TASK_ID);
  LOGI("LooperThread destructor, sent destroy task, waiting...");
  thread.join();
  LOGI("LooperThread destructor, thread killed!");
  methodMap.clear();
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
  char buffer[1];
  while (read(fd, buffer, sizeof(buffer)) > 0) {}
  auto taskId = *reinterpret_cast<uint8_t*>(buffer);
  auto * looperThread = reinterpret_cast<LooperThread*>(data);
  // treating internal destroy task a bit differently
  if (taskId == STOP_THREAD_TASK_ID) {
    ALooper_removeFd(looperThread->aLooper, fd);
    if (close(looperThread->fds[PIPE_IN]) || close(looperThread->fds[PIPE_OUT])) {
      throw std::runtime_error("Failed to close file descriptor!");
    }
    // explicit wake here in order to unblock ALooper_pollAll
    ALooper_wake(looperThread->aLooper);
    ALooper_release(looperThread->aLooper);
    // returning 0 to have this file descriptor and callback unregistered from the looper
    return 0;
  }
  if (looperThread->methodMap.find(taskId) != looperThread->methodMap.end()) {
    looperThread->methodMap[taskId]();
  } else {
    LOGW("Looper callback triggered with taskId=%i, but associated function could not be found!", taskId);
  }
  return 1;
}

uint8_t LooperThread::registerTask(const std::function<void()>& function) {
  lastTakenTaskId++;
  methodMap.emplace(lastTakenTaskId, function);
  return lastTakenTaskId;
}

void LooperThread::scheduleTask(uint8_t id) {
  if (methodMap.find(id) != methodMap.end()) {
    write(fds[PIPE_IN], &id, 1);
  } else {
    LOGW("Task with id=%i could not be run. Did you forget to register it beforehand?", id);
  }
}

} // namespace android
} // namespace engine
