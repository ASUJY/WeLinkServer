#pragma once

#include <cstdint>
#include <mutex>

class SnowflakeGenerator {
public:
    SnowflakeGenerator(uint32_t workerid, uint32_t datacenerid);
    int64_t NextId();

    SnowflakeGenerator(const SnowflakeGenerator&)            = delete;
    SnowflakeGenerator& operator=(const SnowflakeGenerator&) = delete;

private:
    // 等待下一毫秒（处理序列号耗尽）
    int64_t WaitNextMillis(int64_t lastTimestamp);

    // 获取当前毫秒时间戳
    int64_t CurrentTimestampMs();

private:
    // 起始时间戳（2026-01-01 00:00:00 UTC）
    static constexpr int64_t TWEPOCH = 1767225600000;

    static constexpr uint32_t DATACENTER_ID_BITS = 5;
    static constexpr uint32_t WORKER_ID_BITS     = 5;
    static constexpr uint32_t SEQUENCE_BITS      = 12;

    // 各部分最大值
    static constexpr uint32_t MAX_DATACENTER_ID = (1 << DATACENTER_ID_BITS) - 1;
    static constexpr uint32_t MAX_WORKER_ID     = (1 << WORKER_ID_BITS) - 1;
    static constexpr uint32_t MAX_SEQUENCE      = (1 << SEQUENCE_BITS) - 1;

    // 左移偏移量
    static constexpr uint32_t WORKER_ID_SHIFT     = SEQUENCE_BITS;
    static constexpr uint32_t DATACENTER_ID_SHIFT = SEQUENCE_BITS + WORKER_ID_BITS;
    static constexpr uint32_t TIMESTAMP_SHIFT = SEQUENCE_BITS + WORKER_ID_BITS + DATACENTER_ID_BITS;

    uint32_t m_datacenterId;
    uint32_t m_workerId;
    uint64_t m_sequence;
    uint64_t m_lastTimestamp;

    std::mutex m_mtx;
};