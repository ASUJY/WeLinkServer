#include "redis.h"

#include <muduo/base/Logging.h>

namespace {
struct RedisReplyDeleter {
    void operator()(redisReply* reply) const noexcept {
        if (reply) {
            freeReplyObject(reply);
        }
    }
};

using RedisReplyPtr = std::unique_ptr<redisReply, RedisReplyDeleter>;
}  // namespace

Redis::Redis() : m_publishContext(nullptr), m_subscribeContext(nullptr) {}

Redis::~Redis() {
    StopWorker();  // 停止工作线程

    if (m_publishContext != nullptr) {
        redisFree(m_publishContext);
        m_publishContext = nullptr;
    }
    if (m_subscribeContext != nullptr) {
        redisFree(m_subscribeContext);
        m_subscribeContext = nullptr;
    }
}

void Redis::StopWorker() {
    m_stop.store(true, std::memory_order_relaxed);
    m_queueCondv.notify_all();  // 唤醒可能等待任务的线程
    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }

    // 清空任务队列，防止旧任务干扰新连接
    std::lock_guard<std::mutex> lock(m_queueMtx);
    while (!m_taskQueue.empty()) {
        m_taskQueue.pop();
    }
}

void Redis::PushTask(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> lock(m_queueMtx);
        m_taskQueue.push(std::move(task));
    }
    m_queueCondv.notify_one();  // 唤醒一个工作线程处理队列中的任务
}

bool Redis::Connect() {
    // 先清理旧的连接和线程
    StopWorker();

    if (m_publishContext) {
        redisFree(m_publishContext);
        m_publishContext = nullptr;
    }
    if (m_subscribeContext) {
        redisFree(m_subscribeContext);
        m_subscribeContext = nullptr;
    }

    // 负责publish发布消息的上下文连接
    m_publishContext = redisConnect("127.0.0.1", 6379);
    if (nullptr == m_publishContext || m_publishContext->err) {
        LOG_ERROR << "redis connect fail: "
                  << (m_publishContext ? m_publishContext->errstr : "malloc failed");
        redisFree(m_publishContext);
        m_publishContext = nullptr;
        return false;
    }

    // 负责subscribe订阅消息的上下文连接
    m_subscribeContext = redisConnect("127.0.0.1", 6379);
    if (nullptr == m_subscribeContext || m_subscribeContext->err) {
        LOG_ERROR << "subscribe connect fail: "
                  << (m_subscribeContext ? m_subscribeContext->errstr : "malloc failed");

        redisFree(m_subscribeContext);
        m_subscribeContext = nullptr;
        redisFree(m_publishContext);
        m_publishContext = nullptr;
        return false;
    }

    // 设置读超时（1秒），使工作线程能定期检查停止标志
    struct timeval tv = {1, 0};
    redisSetTimeout(m_subscribeContext, tv);
    redisSetTimeout(m_publishContext, tv);

    // 重置停止标志
    m_stop.store(false, std::memory_order_relaxed);
    // 启动工作线程，在单独的线程中，监听redis消息队列上的事件，有消息给业务层进行上报
    m_workerThread = std::thread(&Redis::ProcessSubscription, this);

    LOG_INFO << "connect redis-server success!";
    return true;
}

bool Redis::Publish(uint64_t channel, const std::string& message) {
    if (!m_publishContext) {
        LOG_ERROR << "publish context is null";
        return false;
    }

    RedisReplyPtr reply(static_cast<redisReply*>(
        redisCommand(m_publishContext, "PUBLISH %llu %s", channel, message.c_str())));

    if (nullptr == reply) {
        LOG_ERROR << "publish command failed (no reply)";
        return false;
    }

    if (reply->type == REDIS_REPLY_ERROR) {
        LOG_ERROR << "publish redis err: " << reply->str;
        return false;
    }

    return true;
}

bool Redis::Subscribe(uint64_t channel) {
    if (!m_subscribeContext) {
        LOG_ERROR << "subscribe context is null";
        return false;
    }

    // 将订阅任务推送到工作线程队列
    PushTask([this, channel]() {
        if (REDIS_ERR == redisAppendCommand(this->m_subscribeContext, "SUBSCRIBE %llu", channel)) {
            LOG_ERROR << "subscribe append command failed!";
            return;
        }

        int done = 0;
        while (!done) {
            if (REDIS_ERR == redisBufferWrite(this->m_subscribeContext, &done)) {
                LOG_ERROR << "subscribe buffer write failed!";
                return;
            }
        }
        // 命令已发送，回复将由工作线程的 redisGetReply 接收
    });

    return true;
}

bool Redis::Unsubscribe(uint64_t channel) {
    if (!m_subscribeContext) {
        LOG_ERROR << "subscribe context is null";
        return false;
    }

    PushTask([this, channel]() {
        if (REDIS_ERR ==
            redisAppendCommand(this->m_subscribeContext, "UNSUBSCRIBE %llu", channel)) {
            LOG_ERROR << "unsubscribe append command failed!";
            return;
        }
        // redisBufferWrite可以循环发送缓冲区，直到缓冲区数据发送完毕（done被置为1）
        int done = 0;
        while (!done) {
            if (REDIS_ERR == redisBufferWrite(this->m_subscribeContext, &done)) {
                LOG_ERROR << "unsubscribe buffer write failed!";
                return;
            }
        }
    });

    return true;
}

static bool ParseChannel(redisReply* channelReply, uint64_t& channel) {
    if (!channelReply || channelReply->type != REDIS_REPLY_STRING) return false;

    char* endptr           = nullptr;
    errno                  = 0;
    unsigned long long val = strtoull(channelReply->str, &endptr, 10);

    if (errno == ERANGE || endptr == channelReply->str || *endptr != '\0') {
        LOG_ERROR << "invalid channel number: "
                  << std::string(channelReply->str, channelReply->len);
        return false;
    }
    channel = static_cast<uint64_t>(val);
    return true;
}

void Redis::ProcessSubscription() {
    LOG_INFO << "subscription thread started";
    while (!m_stop.load(std::memory_order_relaxed)) {
        // 1. 处理队列中的任务（如 SUBSCRIBE / UNSUBSCRIBE）
        {
            std::unique_lock<std::mutex> lock(m_queueMtx);
            if (!m_taskQueue.empty()) {
                auto task = std::move(m_taskQueue.front());
                m_taskQueue.pop();
                lock.unlock();
                task();
            } else {
                m_queueCondv.wait_for(lock, std::chrono::milliseconds(100));
            }
            lock.unlock();
        }

        // 2. 尝试从订阅连接读取一条回复（阻塞，但受超时限制）
        redisReply* reply = nullptr;
        int ret           = redisGetReply(m_subscribeContext, (void**)&reply);
        if (ret == REDIS_ERR) {
            // 检查错误类型
            if (m_subscribeContext->err == REDIS_ERR_EOF) {
                // 对端关闭连接，必须退出
                LOG_ERROR << "redis connection EOF";
                m_stop.store(true);
                break;
            } else if (m_subscribeContext->err == REDIS_ERR_IO) {
                // 超时或网络错误
                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                    // 超时，继续循环
                    continue;
                } else {
                    // 其他 I/O 错误（如连接断开），退出
                    LOG_ERROR << "redis IO error: " << m_subscribeContext->errstr;
                    m_stop.store(true);
                    break;
                }
            } else {
                // 其他未知错误
                LOG_ERROR << "redis error: " << m_subscribeContext->errstr;
                m_stop.store(true);
                break;
            }
        }

        // 处理回复
        if (reply) {
            // 订阅回复是数组，且第一个元素是 "subscribe"/"unsubscribe"/"message"
            if (reply->type == REDIS_REPLY_ARRAY && reply->elements >= 3) {
                redisReply* typeReply    = reply->element[0];
                redisReply* channelReply = reply->element[1];
                redisReply* dataReply    = reply->element[2];

                if (typeReply && typeReply->type == REDIS_REPLY_STRING) {
                    std::string typeStr(typeReply->str, typeReply->len);
                    if (typeStr == "message") {
                        // 收到消息，调用回调
                        uint64_t channel = 0;
                        if (ParseChannel(channelReply, channel) && dataReply &&
                            dataReply->type == REDIS_REPLY_STRING) {
                            std::string msg(dataReply->str, dataReply->len);
                            std::lock_guard<std::mutex> cbLock(m_callbackMtx);
                            if (m_notifyMessageHandler) {
                                m_notifyMessageHandler(channel, msg);
                            }
                        }
                    }
                }
            }
            freeReplyObject(reply);
        }
    }
    LOG_INFO << "subscription thread exited";
}

void Redis::InitNotifyHandler(std::function<void(uint64_t, const std::string&)> fn) {
    std::lock_guard<std::mutex> lock(m_callbackMtx);
    this->m_notifyMessageHandler = fn;
}