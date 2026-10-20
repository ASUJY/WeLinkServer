#include <gtest/gtest.h>

#include <condition_variable>
#include <mutex>

#include "redis.h"

namespace {
bool IntegrationEnabled() {
    // 读取环境变量
    const char* enabled = std::getenv("WE_LINK_REDIS_INTEGRATION");
    return enabled != nullptr && std::string(enabled) == "1";
}

void RequireIntegrationRedis() {
    if (!IntegrationEnabled()) {
        // 跳过当前测试，并输出提示信息。
        GTEST_SKIP() << "Set WE_LINK_REDIS_INTEGRATION=1 to run Redis integration tests";
    }
}

int64_t UniqueChannel() {
    static std::atomic<int64_t> sequence{0};
    return 1000000000LL +
           (std::chrono::steady_clock::now().time_since_epoch().count() + sequence.fetch_add(1)) %
               8000000000LL;
}

/**
 * TEST：定义一个测试用例
 * 测试组名称：RedisIntegrationTest
 * 测试用例名称：PublishSubscribeAndUnsubscribe
 */
TEST(RedisIntegrationTest, PublishSubscribeAndUnsubscribe) {
    // Opt in explicitly because this test requires a real Redis server at
    // 127.0.0.1:6379. Start Redis, then set WE_LINK_REDIS_INTEGRATION=1.
    RequireIntegrationRedis();

    std::mutex mutex;
    std::condition_variable condv;
    bool received           = false;  // 标记是否已收到订阅消息
    int64_t receivedChannel = 0;      // 保存收到的频道号
    std::string receivedMessage;      // 收到的消息

    Redis redis;
    // register callback function
    redis.InitNotifyHandler([&](int64_t channel, const std::string& message) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            received        = true;
            receivedChannel = channel;
            receivedMessage = message;
        }
        condv.notify_one();
    });

    // 断言 Redis 连接成功；失败则测试终止并输出提示。
    ASSERT_TRUE(redis.Connect()) << "Redis must be available at 127.0.0.1:6379";

    const int64_t channel = UniqueChannel();
    // 断言订阅指定频道成功
    ASSERT_TRUE(redis.Subscribe(channel));

    // Allow Redis to process SUBSCRIBE before publishing.
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    // 断言向该频道发布消息成功，消息内容含空格。
    ASSERT_TRUE(redis.Publish(channel, "integration-message with spaces"));

    {
        // 对 mutex 加唯一锁；条件变量等待需要 unique_lock。
        std::unique_lock<std::mutex> lock(mutex);
        // 最多等待 3 秒，直到 received == true；断言等待成功。若等待超时，输出提示信息
        ASSERT_TRUE(condv.wait_for(lock, std::chrono::seconds(3), [&] { return received; }))
            << "Timed out waiting for the subscribed message";
        // 期望收到的频道号等于发布时使用的频道号。
        EXPECT_EQ(receivedChannel, channel);
        // 期望收到的消息内容等于发布的消息。
        EXPECT_EQ(receivedMessage, "integration-message with spaces");
    }

    // 断言取消订阅该频道成功
    ASSERT_TRUE(redis.Unsubscribe(channel));
    {
        std::lock_guard<std::mutex> lock(mutex);
        received = false;  // 验证取消订阅后不再收消息。
    }

    // 断言发布一条“不应被送达”的消息成功。
    ASSERT_TRUE(redis.Publish(channel, "must-not-be-delivered"));
    {
        std::unique_lock<std::mutex> lock(mutex);
        // 期望 received 一直为 false，即取消订阅后没再收到消息。
        EXPECT_FALSE(
            condv.wait_for(lock, std::chrono::milliseconds(300), [&] { return received; }));
    }
}

TEST(RedisIntegrationTest, OperationsFailBeforeConnect) {
    RequireIntegrationRedis();
    Redis redis;

    EXPECT_FALSE(redis.Publish(UniqueChannel(), "not-connected"));
    EXPECT_FALSE(redis.Subscribe(UniqueChannel()));
    EXPECT_FALSE(redis.Unsubscribe(UniqueChannel()));
}

TEST(RedisIntegrationTest, HandlesEmptyAndSpecialTextMessages) {
    RequireIntegrationRedis();
    std::mutex mutex;
    std::condition_variable condv;
    std::vector<std::string> received;

    Redis redis;
    // register callback function
    redis.InitNotifyHandler([&](int64_t, const std::string& message) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            received.push_back(message);
        }
        condv.notify_one();
    });

    ASSERT_TRUE(redis.Connect());

    const int64_t channel = UniqueChannel();  // channel id
    ASSERT_TRUE(redis.Subscribe(channel));    // subscribe channel
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    const std::vector<std::string> messages = {"", "with spaces",
                                               "quotes ' \" \\ and punctuation !?"};
    for (const auto& message : messages) {
        ASSERT_TRUE(redis.Publish(channel, message));
    }

    {
        std::unique_lock<std::mutex> lock(mutex);
        ASSERT_TRUE(condv.wait_for(lock, std::chrono::seconds(3), [&] {
            return received.size() == messages.size();
        })) << "Timed out waiting for all text messages";
        EXPECT_EQ(received, messages);
    }
}

TEST(RedisIntegrationTest, ConcurrentPublishCallsDeliverEveryMessage) {
    RequireIntegrationRedis();
    constexpr int kPublisherCount       = 4;
    constexpr int kMessagesPerPublisher = 100;
    constexpr int kExpectedMessages     = kPublisherCount * kMessagesPerPublisher;

    std::mutex mutex;
    std::condition_variable condv;
    std::set<std::string> received;

    Redis redis;
    // register callback function
    redis.InitNotifyHandler([&](int64_t, const std::string& message) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            received.insert(message);
        }
        condv.notify_one();
    });

    ASSERT_TRUE(redis.Connect());

    const int64_t channel = UniqueChannel();
    ASSERT_TRUE(redis.Subscribe(channel));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Multi-thread concurrent publish
    std::atomic<int> failures{0};
    std::vector<std::thread> publishers;
    for (int publisher = 0; publisher < kPublisherCount; ++publisher) {
        publishers.emplace_back([&, publisher] {
            for (int message = 0; message < kMessagesPerPublisher; ++message) {
                const std::string payload =
                    std::to_string(publisher) + ":" + std::to_string(message);
                if (!redis.Publish(channel, payload)) {
                    ++failures;
                }
            }
        });
    }

    for (auto& publisher : publishers) {
        publisher.join();
    }

    EXPECT_EQ(failures.load(), 0);

    {
        std::unique_lock<std::mutex> lock(mutex);
        ASSERT_TRUE(condv.wait_for(lock, std::chrono::seconds(5),
                                   [&] { return received.size() == kExpectedMessages; }))
            << "Received " << received.size() << " of " << kExpectedMessages << " unique messages";
    }
}

TEST(RedisIntegrationTest, CallbackCanBeReplacedWhileMessagesArrive) {
    RequireIntegrationRedis();
    std::atomic<int> callbackCount{0};
    Redis redis;
    redis.InitNotifyHandler([&](int64_t, const std::string&) { ++callbackCount; });

    ASSERT_TRUE(redis.Connect());

    const int64_t channel = UniqueChannel();
    ASSERT_TRUE(redis.Subscribe(channel));
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    std::atomic<bool> publishing{true};
    std::thread publisher([&] {
        int sequence = 0;
        while (publishing.load()) {
            if (!redis.Publish(channel, std::to_string(sequence++))) {
                break;
            }
        }
    });

    for (int i = 0; i < 100; i++) {
        if (i % 2 == 0) {
            redis.InitNotifyHandler([&](int64_t, const std::string&) { ++callbackCount; });
        } else {
            redis.InitNotifyHandler({});
        }
    }

    redis.InitNotifyHandler([&](int64_t, const std::string&) { ++callbackCount; });
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    publishing.store(false);
    publisher.join();

    EXPECT_GT(callbackCount.load(), 0);
}

TEST(RedisIntegrationTest, DestructorStopsObserverWithActiveSubscription) {
    RequireIntegrationRedis();
    auto redis = std::make_unique<Redis>();
    ASSERT_TRUE(redis->Connect());
    ASSERT_TRUE(redis->Subscribe(UniqueChannel()));

    const auto start = std::chrono::steady_clock::now();
    redis.reset();
    EXPECT_LT(std::chrono::steady_clock::now() - start, std::chrono::seconds(2));
}

}  // namespace