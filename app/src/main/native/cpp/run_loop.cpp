#include "run_loop.hpp"

#include <android/looper.h>
#include <fcntl.h>
#include <unistd.h>

// STL
#include <cassert>
#include <memory>

namespace engine {
namespace android {

namespace internal {

Pipe::Pipe() {
    if (pipe(fds_) != 0) {
        throw std::runtime_error("Failed to create pipe");
    }

    if (fcntl(fds_[PIPE_OUT], F_SETFL, O_NONBLOCK)) {
        closeFds();
        throw std::runtime_error("Failed to set pipe read end non-blocking.");
    }
}

Pipe::~Pipe() {
    closeFds();
}

void Pipe::closeFds() {
    close(fds_[PIPE_IN]);
    close(fds_[PIPE_OUT]);
}

ALooperHolder::ALooperHolder(ALooper* alooper) : alooper_(alooper) {
    assert(alooper_);
    ALooper_acquire(alooper_);
}

ALooperHolder::~ALooperHolder() {
    ALooper_release(alooper_);
}

}  // namespace internal

RunLoop::RunLoop(ALooper* alooper) : alooper_(alooper) {
    int ret = ALooper_addFd(
        alooper_.get(), pipe_.outFd(), ALOOPER_POLL_CALLBACK, ALOOPER_EVENT_INPUT,
        [](int fd, int, void* data) -> int {
            int buffer[1];
            while (read(fd, buffer, sizeof(buffer)) > 0) {
            }

            // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
            auto loop = reinterpret_cast<RunLoop*>(data);
            return loop->looperCallback();
        },
        this);
    if (ret != 1) {
        throw std::runtime_error("Failed to add file descriptor to Looper.");
    }
}

int RunLoop::looperCallback() {
    runTasks();
    return 1;
}

void RunLoop::runTasks() {
    std::deque<std::unique_ptr<Task>> tasks{};

    // collect ready to run tasks
    {
        std::lock_guard<std::mutex> lock(mutex_);

        wakeCalled_.clear();

        while (!tasks_.empty()) {
            tasks.emplace_back(std::move(tasks_.front()));
            tasks_.pop_front();
        }
    }

    while (!tasks.empty()) {
        auto task = std::move(tasks.front());
        tasks.pop_front();
        task->operator()();
    }
}

void RunLoop::run() {
    int outFd, outEvents;
    char* outData = nullptr;
    ALooper_pollAll(-1, &outFd, &outEvents, reinterpret_cast<void**>(&outData));
}

void RunLoop::stop() {
    ALooper_wake(alooper_.get());
}

void RunLoop::wake() {
    if (wakeCalled_.test_and_set(std::memory_order_acquire)) {
        return;
    }

    if (write(pipe_.inFd(), "\n", 1) == -1) {
        throw std::runtime_error("Failed to write to file descriptor.");
    }
}

void RunLoop::schedule(std::unique_ptr<Task> task) {
    assert(task);

    {
        std::lock_guard<std::mutex> lock(mutex_);
        tasks_.emplace_back(std::move(task));
        wake();
    }
}

}  // namespace android
}  // namespace engine
