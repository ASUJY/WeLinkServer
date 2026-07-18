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
    if (m_publishContext != nullptr) {
        redisFree(m_publishContext);
        m_publishContext = nullptr;
    }
    if (m_subscribeContext != nullptr) {
        redisFree(m_subscribeContext);
        m_subscribeContext = nullptr;
    }
}

bool Redis::Connect() {
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

    // 在单独的线程中，监听通道上的事件，有消息给业务层进行上报
    std::thread t([&]() { ObserverChannelMessage(); });
    t.detach();

    LOG_INFO << "connect redis-server success!";
    return true;
}

bool Redis::Publish(int64_t channel, const std::string& message) {
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

bool Redis::Subscribe(int64_t channel) {
    if (!m_subscribeContext) {
        LOG_ERROR << "subscribe context is null";
        return false;
    }

    if (REDIS_ERR == redisAppendCommand(this->m_subscribeContext, "SUBSCRIBE %llu", channel)) {
        LOG_ERROR << "subscribe append command failed!";
        return false;
    }

    int done = 0;
    while (!done) {
        if (REDIS_ERR == redisBufferWrite(this->m_subscribeContext, &done)) {
            LOG_ERROR << "subscribe buffer write failed!";
            return false;
        }
    }

    return true;
}

bool Redis::Unsubscribe(int64_t channel) {
    if (!m_subscribeContext) {
        LOG_ERROR << "subscribe context is null";
        return false;
    }

    if (REDIS_ERR == redisAppendCommand(this->m_subscribeContext, "UNSUBSCRIBE %llu", channel)) {
        LOG_ERROR << "unsubscribe append command failed!";
        return false;
    }
    // redisBufferWrite可以循环发送缓冲区，直到缓冲区数据发送完毕（done被置为1）
    int done = 0;
    while (!done) {
        if (REDIS_ERR == redisBufferWrite(this->m_subscribeContext, &done)) {
            LOG_ERROR << "unsubscribe buffer write failed!";
            return false;
        }
    }

    return true;
}

void Redis::ObserverChannelMessage() {
    redisReply* reply = nullptr;
    while (REDIS_OK == redisGetReply(this->m_subscribeContext, (void**)&reply)) {
        // 订阅收到的消息是一个带三元素的数组
        if (reply != nullptr && reply->element[2] != nullptr && reply->element[2]->str != nullptr) {
            // 给业务层上报通道上发生的消息
            m_notifyMessageHandler(atoi(reply->element[1]->str), reply->element[2]->str);
        }

        freeReplyObject(reply);
    }

    LOG_ERROR << ">>>>>>>>>>>>> observer_channel_message quit <<<<<<<<<<<<<";
}

void Redis::InitNotifyHandler(std::function<void(int64_t, const std::string&)> fn) {
    std::lock_guard<std::mutex> lock(m_callbackMtx);
    this->m_notifyMessageHandler = fn;
}