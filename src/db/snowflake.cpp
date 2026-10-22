#include "snowflake.h"

#include <stdexcept>

SnowflakeGenerator::SnowflakeGenerator(uint32_t workerid, uint32_t datacenerid)
    : m_datacenterId(datacenerid), m_workerId(workerid), m_sequence(0), m_lastTimestamp(0) {
    if (workerid > MAX_WORKER_ID) {
        throw std::invalid_argument("Worker ID exceeds the maximum limit!");
    }
    if (datacenerid > MAX_DATACENTER_ID) {
        throw std::invalid_argument("Datacenter ID exceeds the maximux limit!");
    }
}

int64_t SnowflakeGenerator::NextId() {
    uint64_t timestamp = CurrentTimestampMs();

    std::lock_guard<std::mutex> locker(m_mtx);
    if (timestamp < m_lastTimestamp) {
        throw std::runtime_error("Clock rollback, ID generation rejected");
    }

    if (timestamp == m_lastTimestamp) {
        m_sequence = (m_sequence + 1) % MAX_SEQUENCE;  // 同一毫秒内，序列号自增
        if (m_sequence == 0) {
            timestamp = WaitNextMillis(m_lastTimestamp);  // 序列号溢出，等到下一毫秒
        }
    } else {
        m_sequence = 0;  // 不同毫秒，序列号重置为0
    }

    m_lastTimestamp = timestamp;

    // 生成64位ID
    return ((timestamp - TWEPOCH) << TIMESTAMP_SHIFT) | (m_datacenterId << DATACENTER_ID_SHIFT) |
           (m_workerId << WORKER_ID_SHIFT) | m_sequence;
}

int64_t SnowflakeGenerator::WaitNextMillis(int64_t lastTimestamp) {
    int64_t timestamp = CurrentTimestampMs();
    while (timestamp <= lastTimestamp) {
        timestamp = CurrentTimestampMs();
    }
    return timestamp;
}

int64_t SnowflakeGenerator::CurrentTimestampMs() {
    auto now = std::chrono::system_clock::now();
    auto ms  = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch());
    return static_cast<int64_t>(ms.count());
}