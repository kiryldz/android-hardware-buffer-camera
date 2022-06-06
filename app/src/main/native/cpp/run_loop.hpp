#pragma once

// STL
#include <deque>
#include <map>
#include <memory>
#include <mutex>

class ALooper;

namespace engine {
namespace android {

namespace internal {

class Pipe {
public:
    Pipe();
    ~Pipe();
    int inFd() const { return fds_[PIPE_IN]; }
    int outFd() const { return fds_[PIPE_OUT]; }

private:
    void closeFds();
    const int PIPE_OUT = 0;
    const int PIPE_IN = 1;
    int fds_[2];
};

class ALooperHolder {
public:
    explicit ALooperHolder(ALooper* alooper);
    ~ALooperHolder();
    ALooper* get() const { return alooper_; }

private:
    ALooper* alooper_;
};

}  // namespace internal

class RunLoop {
public:
    using Task = std::function<void()>;

    explicit RunLoop(ALooper*);
    ~RunLoop() = default;

    void run();
    void stop();

    void schedule(std::unique_ptr<Task> task);

private:
    int looperCallback();
    void runTasks();
    void wake();

    internal::Pipe pipe_;
    internal::ALooperHolder alooper_;
    std::atomic_flag wakeCalled_ = ATOMIC_FLAG_INIT;
    std::mutex mutex_;
    std::deque<std::unique_ptr<Task>> tasks_;
};

}  // namespace android
}  // namespace engine