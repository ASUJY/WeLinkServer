#include "redis.h"

#include <muduo/base/Logging.h>
#include <poll.h>
#include <sys/socket.h>
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
    m_stop.store(true);
    if (m_subscribeContext != nullptr) {
        // 关闭订阅连接底层 socket 的读写
        shutdown(m_subscribeContext->fd, SHUT_RDWR);
    }
    if (m_workerThread.joinable()) {
        m_workerThread.join();
    }
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
    {
        std::lock_guard<std::mutex> lock(m_publishMtx);
        std::lock_guard<std::mutex> lock1(m_subscrMtx);

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
    }

    m_stop.store(false);

    // 在单独的线程中，监听通道上的事件，有消息给业务层进行上报
    m_workerThread = std::thread([this]() { ObserverChannelMessage(); });

    LOG_INFO << "connect redis-server success!";
    return true;
}

bool Redis::Publish(int64_t channel, const std::string& message) {
    std::lock_guard<std::mutex> lock(m_publishMtx);
    if (!m_publishContext) {
        LOG_ERROR << "publish context is null";
        return false;
    }

    RedisReplyPtr reply(static_cast<redisReply*>(redisCommand(
        m_publishContext, "PUBLISH %lld %s", static_cast<long long>(channel), message.c_str())));

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
    {
        std::lock_guard<std::mutex> lock(m_subscrMtx);
        if (!m_subscribeContext) {
            LOG_ERROR << "subscribe context is null";
            return false;
        }

        if (REDIS_ERR == redisAppendCommand(this->m_subscribeContext, "SUBSCRIBE %lld",
                                            static_cast<long long>(channel))) {
            LOG_ERROR << "subscribe append command failed!";
            return false;
        }
    }

    int done = 0;
    while (!done) {
        {
            std::lock_guard<std::mutex> lock(m_subscrMtx);

            if (REDIS_ERR == redisBufferWrite(this->m_subscribeContext, &done)) {
                LOG_ERROR << "subscribe buffer write failed!";
                return false;
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(m_subscribeChanelMtx);
        m_subscribedChannels.insert(channel);
    }

    return true;
}

bool Redis::Unsubscribe(int64_t channel) {
    {
        std::lock_guard<std::mutex> lock(m_subscrMtx);
        if (!m_subscribeContext) {
            LOG_ERROR << "subscribe context is null";
            return false;
        }

        if (REDIS_ERR == redisAppendCommand(this->m_subscribeContext, "UNSUBSCRIBE %lld",
                                            static_cast<long long>(channel))) {
            LOG_ERROR << "unsubscribe append command failed!";
            return false;
        }
    }
    // redisBufferWrite可以循环发送缓冲区，直到缓冲区数据发送完毕（done被置为1）
    int done = 0;
    while (!done) {
        {
            std::lock_guard<std::mutex> lock(m_subscrMtx);
            if (REDIS_ERR == redisBufferWrite(this->m_subscribeContext, &done)) {
                LOG_ERROR << "unsubscribe buffer write failed!";
                return false;
            }
        }
    }

    {
        std::lock_guard<std::mutex> lock(m_subscribeChanelMtx);
        m_subscribedChannels.erase(channel);
    }

    return true;
}

void Redis::ObserverChannelMessage() {
    while (!m_stop.load()) {
        pollfd descriptor{};
        {
            std::lock_guard<std::mutex> lock(m_subscrMtx);
            descriptor.fd = m_subscribeContext->fd;
        }
        descriptor.events = POLLIN;
        const int ready   = poll(&descriptor, 1, 100);
        if (ready == 0) continue;
        if (ready < 0 || (descriptor.revents & (POLLERR | POLLHUP | POLLNVAL))) {
            break;
        }

        redisReply* reply = nullptr;
        {
            std::lock_guard<std::mutex> lock(m_subscrMtx);
            if (redisGetReply(this->m_subscribeContext, reinterpret_cast<void**>(&reply)) !=
                REDIS_OK) {
                break;
            }
        }

        RedisReplyPtr replyPtr(reply);
        // Only forward actual Pub/Sub messages; SUBSCRIBE/UNSUBSCRIBE
        // acknowledgements have a different shape and are not user messages.
        if (reply && reply->type == REDIS_REPLY_ARRAY && reply->elements >= 3 &&
            reply->element[0] && reply->element[0]->str &&
            std::string(reply->element[0]->str) == "message" && reply->element[1] &&
            reply->element[1]->str && reply->element[2] && reply->element[2]->str) {
            std::function<void(int64_t, std::string)> callback;
            {
                std::lock_guard<std::mutex> lock(m_callbackMtx);
                callback = m_notifyMessageHandler;
            }
            if (callback) {
                callback(std::strtoll(reply->element[1]->str, nullptr, 10), reply->element[2]->str);
            }
        }
    }
    LOG_ERROR << ">>>>>>>>>>>>> observer_channel_message quit <<<<<<<<<<<<<";
}

void Redis::InitNotifyHandler(std::function<void(int64_t, const std::string&)> fn) {
    std::lock_guard<std::mutex> lock(m_callbackMtx);
    this->m_notifyMessageHandler = fn;
}