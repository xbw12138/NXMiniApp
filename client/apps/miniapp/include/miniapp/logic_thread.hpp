//
// miniapp: 逻辑线程。
//
// QuickJS、定时器、网络/SMB 回调统一放到这条线程执行，UI 线程只负责布局、
// 绘制和输入，避免脚本或数据加载把帧拖住。UI 线程通过 post() 投递任务，
// 需要回 UI 的宿主操作走 brls::sync。
//

#pragma once

#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>

#include <pthread.h>

namespace miniapp {

class LogicThread {
public:
    LogicThread();
    ~LogicThread();

    LogicThread(const LogicThread&) = delete;
    LogicThread& operator=(const LogicThread&) = delete;

    /// 投递任务。线程不可用（创建失败/已停止）时在调用线程同步执行，
    /// 保证调用方语义不变。
    void post(std::function<void()> task);

    /// 停止接收任务并 join。返回后线程不再执行任何任务；
    /// 队列里剩余任务的析构（例如释放 JsRuntime 引用）在调用线程完成。
    void stop();

    bool running() const { return started; }

    /// 线程入口，仅给 pthread 入口函数用。
    void run();

private:
    std::mutex mutex;
    std::condition_variable cv;
    std::deque<std::function<void()>> tasks;
    bool stopping = false;
    bool started = false;
    pthread_t handle{};
};

}  // namespace miniapp
