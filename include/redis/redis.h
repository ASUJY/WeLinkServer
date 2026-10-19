#ifndef REDIS_H
#define REDIS_H

#include <hiredis/hiredis.h>

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <set>
#include <thread>

class Redis {
public:
    Redis();
    Redis(const Redis&)             = delete;
    Redis& operator=(const Redis&)  = delete;
    Redis(Redis&& other)            = delete;
    Redis& operator=(Redis&& other) = delete;
    ~Redis();

    // 连接redis服务器
    bool Connect();
    // 往redis消息队列发布消息
    bool Publish(int64_t channel, const std::string& message);
    // 在redis消息队列中订阅消息
    bool Subscribe(int64_t channel);
    bool Unsubscribe(int64_t channel);

    // 设置回调(注册 Redis 通知/订阅回调)
    void InitNotifyHandler(std::function<void(int64_t, const std::string&)> fn);

    void ObserverChannelMessage();

private:
    redisContext* m_publishContext   = nullptr;  // 发布上下文（仅主线程使用）
    redisContext* m_subscribeContext = nullptr;  // 订阅上下文（由工作线程独占操作）

    std::atomic<bool> m_stop{false};
    std::thread m_workerThread;
    std::mutex m_queueMtx;
    std::condition_variable m_queueCondv;
    std::queue<std::function<void()>> m_taskQueue;

    std::mutex m_callbackMtx;
    std::function<void(int64_t, std::string)> m_notifyMessageHandler;

    std::mutex m_subscribeMtx;
    std::set<int64_t> m_subscribedChannels;  // 记录本地已订阅的频道
};

#endif