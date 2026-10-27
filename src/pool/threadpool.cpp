#include "threadpool.h"

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