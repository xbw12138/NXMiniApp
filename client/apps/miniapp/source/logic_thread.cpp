//
// miniapp: 逻辑线程实现。
//

#include "miniapp/logic_thread.hpp"

#include <borealis/core/logger.hpp>

#include <exception>
#include <utility>

namespace miniapp {

namespace {

void* logicThreadEntry(void* raw) {
    static_cast<LogicThread*>(raw)->run();
    return nullptr;
}

}  // namespace

LogicThread::LogicThread() {
    // 与仓库里其它后台线程一致：用 pthread 而不是 std::thread，创建失败时只降级为
    // 「调用线程同步执行」，不会因为线程对象析构时仍 joinable 直接 terminate。
    pthread_attr_t attr;
    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
    pthread_attr_setstacksize(&attr, 1024 * 1024);
    const int rc = pthread_create(&handle, &attr, logicThreadEntry, this);
    pthread_attr_destroy(&attr);
    if (rc != 0) {
        brls::Logger::error("miniapp: logic thread create failed rc={}, run inline", rc);
        handle = pthread_t{};
        started = false;
        return;
    }
    started = true;
}

LogicThread::~LogicThread() { stop(); }

void LogicThread::post(std::function<void()> task) {
    if (!task) return;
    bool inlineRun = !started;
    if (!inlineRun) {
        std::lock_guard<std::mutex> lock(mutex);
        if (stopping) {
            inlineRun = true;
        } else {
            tasks.push_back(std::move(task));
        }
    }
    if (inlineRun) {
        // 逻辑线程不可用/已停止：直接在当前线程执行，行为与旧版本一致。
        task();
        return;
    }
    cv.notify_one();
}

void LogicThread::stop() {
    if (!started) return;
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (stopping) return;
        stopping = true;
    }
    cv.notify_all();
    pthread_join(handle, nullptr);
    handle = pthread_t{};
    started = false;

    // 线程已退出，队列里剩下的任务在这里（调用线程）析构，
    // 让 JsRuntime 等 shared_ptr 的释放有确定的线程。
    std::deque<std::function<void()>> leftover;
    {
        std::lock_guard<std::mutex> lock(mutex);
        leftover.swap(tasks);
    }
    leftover.clear();
}

void LogicThread::run() {
    for (;;) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(mutex);
            cv.wait(lock, [this] { return stopping || !tasks.empty(); });
            if (stopping) return;
            task = std::move(tasks.front());
            tasks.pop_front();
        }
        try {
            task();
        } catch (const std::exception& e) {
            brls::Logger::error("miniapp: logic task threw: {}", e.what());
        } catch (...) {
            brls::Logger::error("miniapp: logic task threw unknown exception");
        }
    }
}

}  // namespace miniapp
