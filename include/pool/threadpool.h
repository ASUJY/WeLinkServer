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

ThreadPool::ThreadPool(int threadNumber, int maxQueueSize)
    : m_threadNumber(threadNumber), m_maxQueueSize(maxQueueSize) {
    if (m_threadNumber <= 0 || m_maxQueueSize <= 0) {
        LOG_ERROR << "Threadpool constructor: threadNumber and "
                     "maxQueueSize must be positive";
        throw std::invalid_argument(
            "ThreadPool constructor: threadNumber "
            "and maxQueueSize must be positive");
    }
    try {
        m_threads.reserve(static_cast<std::size_t>(m_threadNumber));
        for (int i = 0; i < m_threadNumber; ++i) {
            m_threads.emplace_back(&ThreadPool::Worker, this);  // 试试不用&
            LOG_INFO << "create the " << i << "th thread";
        }
    } catch (const std::exception& e) {
        LOG_ERROR << "create thread failed: " << e.what();
        m_stop.store(true);
        m_condv.notify_all();
        for (auto& t : m_threads) {
            if (t.joinable()) {
                t.join();
            }
        }
        throw std::runtime_error(std::string("create thread failed: ") + e.what());
    }
}

ThreadPool::~ThreadPool() {
    m_stop.store(true);
    m_condv.notify_all();
    for (std::thread& th : m_threads) {
        if (th.joinable()) {
            th.join();
        }
    }
}

void ThreadPool::Worker() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> locker(m_queueMtx);
            m_condv.wait(locker, [this] { return m_stop.load() || !m_tasks.empty(); });
            if (m_stop.load() && m_tasks.empty()) {
                return;
            }
            task = std::move(m_tasks.front());
            m_tasks.pop();
        }
        try {
            task();
        } catch (const std::exception& e) {
            LOG_ERROR << "task threw exception: " << e.what();
        } catch (...) {
            LOG_ERROR << "task threw unknown exception";
        }
    }
}