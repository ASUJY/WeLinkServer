#include "snowflake.h"

#include <gtest/gtest.h>

#include <thread>
#include <unordered_set>

// 测试正常生成ID
TEST(SnowflakeTest, GenerateNormalId) {
    SnowflakeGenerator gen(1, 1);
    int64_t id1 = gen.NextId();
    int64_t id2 = gen.NextId();
    ASSERT_TRUE(id1 > 0);
    ASSERT_TRUE(id2 > id1);
}

// 测试workerId超限抛异常
TEST(SnowflakeTest, WorkerIdOverflowThrow) {
    ASSERT_THROW(SnowflakeGenerator gen(32, 1), std::invalid_argument);
}

// 多线程并发生成无重复ID
TEST(SnowflakeTest, MultiThreadNoDuplicate) {
    SnowflakeGenerator gen(1, 1);
    std::unordered_set<int64_t> ids;
    std::mutex mtx;
    auto func = [&]() {
        for (int i = 0; i < 1000; ++i) {
            int64_t id = gen.NextId();
            std::lock_guard<std::mutex> lock(mtx);
            ASSERT_EQ(ids.count(id), 0);
            ids.insert(id);
        }
    };
    std::thread t1(func), t2(func);
    t1.join();
    t2.join();
    ASSERT_EQ(ids.size(), 2000);
}