#pragma once

#include <muduo/base/Logging.h>

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

class ThreadPool {
public:
    explicit ThreadPool(int threadNumber = 8, int maxQueueSize = 10000);

    ThreadPool(const ThreadPool&)            = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&)                 = delete;
    ThreadPool& operator=(ThreadPool&&)      = delete;

    ~ThreadPool();

    template <typename F, typename... Args>
    auto AppendQueue(bool& accepted, F&& f, Args&&... args)
        -> std::future<typename std::result_of<F(Args...)>::type>;

private:
    void Worker();

private:
    int m_threadNumber{0};
    std::vector<std::thread> m_threads;
    int m_maxQueueSize{0};                      // 最大待处理任务数量
    std::queue<std::function<void()>> m_tasks;  // 任务队列
    std::mutex m_queueMtx;

    std::atomic<bool> m_stop{false};
    std::condition_variable m_condv;
};

template <typename F, typename... Args>
auto ThreadPool::AppendQueue(bool& accepted, F&& f, Args&&... args)
    -> std::future<typename std::result_of<F(Args...)>::type> {
    using return_type = typename std::result_of<F(Args...)>::type;

    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...));  // 包装任务
    std::future<return_type> res = task->get_future();                // 获取异步结果

    {
        std::lock_guard<std::mutex> lock(m_queueMtx);
        if (m_stop.load() || static_cast<int>(m_tasks.size()) >= m_maxQueueSize) {
            accepted = false;
            LOG_WARN << "ThreadPool::Append(): threadpool task queue "
                        "is full (append failed)!!!";
            return res;
        }
        m_tasks.emplace([task]() { (*task)(); });  // 任务入队
        accepted = true;
    }

    m_condv.notify_one();
    return res;
}
