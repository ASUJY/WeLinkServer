#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstring>
#include <exception>
#include <iostream>
#include <limits>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "common.h"
#include "json.hpp"

namespace {
using Clock                          = std::chrono::steady_clock;
constexpr uint32_t kMaxMessageLength = 1024 * 1024;
constexpr int kDefaultTimeoutMs      = 5000;

struct Options {
    std::string host     = "127.0.0.1";
    int port             = 9999;
    int clients          = 16;
    int durationSeconds  = 30;
    int timeoutMs        = kDefaultTimeoutMs;
    std::string password = "LoadTest-Password-123";
    std::string avatar   = "load-test-avatar";
};

struct WorkerResult {
    uint64_t attempted = 0;
    uint64_t succeeded = 0;
    uint64_t failed    = 0;
    bool connected     = false;
    std::string error;
    std::array<uint64_t, 64> latencyHistogram{};
    uint64_t minLatencyUs = std::numeric_limits<uint64_t>::max();
    uint64_t maxLatencyUs = 0;
};

bool ParsePositiveInt(const std::string& text, int& value) {
    try {
        size_t consumed   = 0;
        const long parsed = std::stol(text, &consumed);
        if (consumed != text.size() || parsed < 1 || parsed > std::numeric_limits<int>::max()) {
            return false;
        }
        value = static_cast<int>(parsed);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

void PrintUsage(const char* program) {
    std::cout << "Usage: " << program
              << " [--host HOST] [--port PORT] [--clients N] [--duration-sec N]"
                 " [--timeout-ms N] [--password TEXT] [--avatar TEXT]\n"
              << "Defaults: host=127.0.0.1 port=9999 clients=16 duration-sec=30"
                 " timeout-ms=5000\n";
}

bool ParseOptions(int argc, char** argv, Options& options) {
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            PrintUsage(argv[0]);
            return false;
        }
        if (i + 1 >= argc) {
            std::cerr << "Missing value for " << arg << "\n";
            return false;
        }
        const std::string value = argv[++i];
        if (arg == "--host") {
            options.host = value;
        } else if (arg == "--port") {
            if (!ParsePositiveInt(value, options.port) || options.port > 65535) {
                std::cerr << "Invalid port: " << value << "\n";
                return false;
            }
        } else if (arg == "--clients") {
            if (!ParsePositiveInt(value, options.clients) || options.clients > 1024) {
                std::cerr << "--clients must be between 1 and 1024\n";
                return false;
            }
        } else if (arg == "--duration-sec") {
            if (!ParsePositiveInt(value, options.durationSeconds) ||
                options.durationSeconds > 86400) {
                std::cerr << "--duration-sec must be between 1 and 86400\n";
                return false;
            }
        } else if (arg == "--timeout-ms") {
            if (!ParsePositiveInt(value, options.timeoutMs) || options.timeoutMs > 600000) {
                std::cerr << "--timeout-ms must be between 1 and 600000\n";
                return false;
            }
        } else if (arg == "--password") {
            options.password = value;
        } else if (arg == "--avatar") {
            options.avatar = value;
        } else {
            std::cerr << "Unknown option: " << arg << "\n";
            return false;
        }
    }
    if (options.host.empty() || options.password.empty()) {
        std::cerr << "Host and password must not be empty\n";
        return false;
    }
    return true;
}

int RemainingTimeoutMs(Clock::time_point deadline) {
    const auto remaining =
        std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now());
    if (remaining.count() <= 0) return 0;
    return static_cast<int>(
        std::min<int64_t>(remaining.count() + 1, std::numeric_limits<int>::max()));
}

// 使用 poll() 等待 socket 达到指定的 I/O 状态，直到操作成功、超时或发生错误。
bool WaitForSocket(int fd, short events, Clock::time_point deadline, std::string& error) {
    while (true) {
        pollfd descriptor{};
        descriptor.fd     = fd;
        descriptor.events = events;
        const int result  = poll(&descriptor, 1, RemainingTimeoutMs(deadline));
        if (result > 0) {
            if (descriptor.revents & POLLNVAL) {
                error = "invalid socket descriptor";
                return false;
            }
            if (descriptor.revents & (events | POLLERR | POLLHUP)) {
                return true;
            }
            continue;
        }
        if (result == 0) {
            error = "socket operation timed out";
            return false;
        }
        if (errno == EINTR) continue;
        error = std::string("poll failed: ") + std::strerror(errno);
        return false;
    }
}

// 根据 Options 中的服务器地址和端口，建立 TCP 连接，并返回 socket 文件描述符；连接失败则返回 -1。
int Connect(const Options& options, std::string& error) {
    addrinfo hints{};
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo* addresses        = nullptr;
    const std::string portText = std::to_string(options.port);
    const int lookup = getaddrinfo(options.host.c_str(), portText.c_str(), &hints, &addresses);
    if (lookup != 0) {
        error = std::string("address lookup failed: ") + gai_strerror(lookup);
        return -1;
    }

    int connectedFd = -1;
    for (addrinfo* address = addresses; address != nullptr; address = address->ai_next) {
        const int fd = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (fd < 0) continue;

        const int originalFlags = fcntl(fd, F_GETFL, 0);
        if (originalFlags < 0 || fcntl(fd, F_SETFL, originalFlags | O_NONBLOCK) < 0) {
            error = std::string("fcntl failed: ") + std::strerror(errno);
            close(fd);
            continue;
        }

        int result = connect(fd, address->ai_addr, address->ai_addrlen);
        if (result < 0 && errno != EINPROGRESS) {
            error = std::string("connect failed: ") + std::strerror(errno);
            close(fd);
            continue;
        }
        if (result < 0) {
            const auto deadline = Clock::now() + std::chrono::milliseconds(options.timeoutMs);
            if (!WaitForSocket(fd, POLLOUT, deadline, error)) {
                close(fd);
                continue;
            }
            int socketError  = 0;
            socklen_t length = sizeof(socketError);
            if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &socketError, &length) < 0 ||
                socketError != 0) {
                if (socketError != 0) errno = socketError;
                error = std::string("connect failed: ") + std::strerror(errno);
                close(fd);
                continue;
            }
        }

        // Keep the socket nonblocking so the send/receive timeout remains effective.
        int enabled = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &enabled, sizeof(enabled));
        connectedFd = fd;
        break;
    }
    freeaddrinfo(addresses);
    if (connectedFd < 0 && error.empty()) {
        error = "could not open a TCP connection";
    }
    return connectedFd;
}

// 将指定长度的数据全部发送到 socket，直到发送完成、超时或发生错误。
bool SendAll(int fd, const char* data, size_t size, int timeoutMs, std::string& error) {
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    size_t sent         = 0;
    while (sent < size) {
        if (!WaitForSocket(fd, POLLOUT, deadline, error)) return false;
        const ssize_t count = send(fd, data + sent, size - sent, MSG_NOSIGNAL);
        if (count > 0) {
            sent += static_cast<size_t>(count);
            continue;
        }
        if (count < 0 && (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)) continue;
        error = std::string("send failed: ") + std::strerror(errno);
        return false;
    }
    return true;
}

// 从 socket 中接收指定字节数的数据，直到收满、超时或发生错误。
bool ReceiveExact(int fd, char* data, size_t size, int timeoutMs, std::string& error) {
    const auto deadline = Clock::now() + std::chrono::milliseconds(timeoutMs);
    size_t received     = 0;
    while (received < size) {
        if (!WaitForSocket(fd, POLLIN, deadline, error)) return false;
        const ssize_t count = recv(fd, data + received, size - received, 0);
        if (count > 0) {
            received += static_cast<size_t>(count);
            continue;
        }
        if (count == 0) {
            error = "server closed the connection";
            return false;
        }
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) continue;
        error = std::string("recv failed: ") + std::strerror(errno);
        return false;
    }
    return true;
}

// 向服务器发送用户注册请求，接收并解析服务器响应，判断注册是否成功，同时统计请求延迟。
bool SendRegisterAndReceive(int fd, const Options& options, const std::string& phone,
                            const std::string& username, uint64_t& latencyUs, std::string& error) {
    const nlohmann::json request = {{"msgtype", static_cast<int>(E_MSG_TYPE::REG_MSG)},
                                    {"data",
                                     {{"phone", phone},
                                      {"username", username},
                                      {"password", options.password},
                                      {"avatar", options.avatar}}}};
    const std::string body       = request.dump();
    if (body.empty() || body.size() > kMaxMessageLength) {
        error = "registration request exceeds the protocol frame limit";
        return false;
    }

    const uint32_t networkLength = htonl(static_cast<uint32_t>(body.size()));
    std::string frame;
    frame.assign(reinterpret_cast<const char*>(&networkLength), sizeof(networkLength));
    frame += body;
    const auto started = Clock::now();
    if (!SendAll(fd, frame.data(), frame.size(), options.timeoutMs, error)) {
        return false;
    }

    uint32_t responseLength = 0;
    if (!ReceiveExact(fd, reinterpret_cast<char*>(&responseLength), sizeof(responseLength),
                      options.timeoutMs, error)) {
        return false;
    }
    const uint32_t bodyLength = ntohl(responseLength);
    if (bodyLength == 0 || bodyLength > kMaxMessageLength) {
        error = "server returned an invalid frame length";
        return false;
    }
    std::string responseBody(bodyLength, '\0');
    if (!ReceiveExact(fd, &responseBody[0], responseBody.size(), options.timeoutMs, error)) {
        return false;
    }
    latencyUs = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - started).count());

    nlohmann::json response;
    try {
        response = nlohmann::json::parse(responseBody);
    } catch (const nlohmann::json::exception& exception) {
        error = std::string("server returned invalid JSON: ") + exception.what();
        return false;
    }
    if (!response.is_object() || !response.contains("msgtype") ||
        !response["msgtype"].is_number_integer() ||
        response["msgtype"].get<int>() != static_cast<int>(E_MSG_TYPE::REG_MSG_ACK) ||
        !response.contains("data") || !response["data"].is_object() ||
        !response["data"].contains("errtype") || !response["data"]["errtype"].is_number_integer()) {
        error = "server returned an unexpected registration response";
        return false;
    }
    if (response["data"]["errtype"].get<int>() !=
        static_cast<int>(E_ERR_TYPE::REG_MSG_ACK_SUCCESS)) {
        error = "server rejected registration for user " + username;
        return false;
    }
    return true;
}

std::string FormatPhone(uint64_t runNonce, uint64_t sequence) {
    constexpr uint64_t kPhoneSpace = 10000000000ULL;
    const uint64_t suffix          = (runNonce + sequence) % kPhoneSpace;
    std::ostringstream phone;
    phone << '1';
    phone.width(10);
    phone.fill('0');
    phone << suffix;
    return phone.str();
}

// 记录一次请求的延迟，更新最小延迟和最大延迟
void RecordLatency(WorkerResult& result, uint64_t latencyUs) {
    size_t bucket       = 0;
    uint64_t upperBound = 1;
    while (latencyUs > upperBound && bucket < result.latencyHistogram.size() - 1) {
        upperBound <<= 1;
        ++bucket;
    }
    ++result.latencyHistogram[bucket];
    result.minLatencyUs = std::min(result.minLatencyUs, latencyUs);
    result.maxLatencyUs = std::max(result.maxLatencyUs, latencyUs);
}

uint64_t Percentile(const std::array<uint64_t, 64>& histogram, uint64_t sampleCount,
                    uint64_t percentile) {
    if (sampleCount == 0) return 0;
    const uint64_t rank  = std::max<uint64_t>(1, (sampleCount * percentile + 99) / 100);
    uint64_t accumulated = 0;
    for (size_t bucket = 0; bucket < histogram.size(); ++bucket) {
        accumulated += histogram[bucket];
        if (accumulated >= rank) {
            return bucket == 63 ? std::numeric_limits<uint64_t>::max() : (uint64_t{1} << bucket);
        }
    }
    return std::numeric_limits<uint64_t>::max();
}
}  // namespace

class RegisterLoadGenerator {
public:
    explicit RegisterLoadGenerator(Options options)
        : m_options(std::move(options)),
          m_runNonce(CreateRunNonce()),
          m_results(static_cast<size_t>(m_options.clients)) {}

    int Run() {
        if (!CheckDescriptorLimit()) return 2;
        if (!StartWorkers()) return 2;

        const auto endTime = m_startTime + std::chrono::seconds(m_options.durationSeconds);
        std::this_thread::sleep_until(endTime);
        for (auto& worker : m_workers) {
            worker.join();
        }
        return PrintSummary(Clock::now());
    }

private:
    static uint64_t CreateRunNonce() {
        const auto nonceSource =
            std::chrono::high_resolution_clock::now().time_since_epoch().count();
        return (static_cast<uint64_t>(nonceSource) ^ (static_cast<uint64_t>(getpid()) << 32)) %
               10000000000ULL;
    }

    bool CheckDescriptorLimit() const {
        rlimit descriptorLimit{};
        if (getrlimit(RLIMIT_NOFILE, &descriptorLimit) == 0 &&
            descriptorLimit.rlim_cur != RLIM_INFINITY &&
            static_cast<rlim_t>(m_options.clients + 32) > descriptorLimit.rlim_cur) {
            std::cerr << "client count exceeds the available file descriptor limit; lower "
                         "--clients or raise ulimit -n\n";
            return false;
        }
        return true;
    }

    bool StartWorkers() {
        m_workers.reserve(static_cast<size_t>(m_options.clients));
        const auto startupStarted = Clock::now();
        try {
            for (int workerId = 0; workerId < m_options.clients; ++workerId) {
                m_workers.emplace_back(&RegisterLoadGenerator::RunWorker, this, workerId);
            }
        } catch (const std::exception& exception) {
            {
                std::lock_guard<std::mutex> lock(m_startMtx);
                m_startWorkers = true;
            }
            m_startCondv.notify_all();
            for (auto& worker : m_workers) worker.join();
            std::cerr << "could not create client workers: " << exception.what() << "\n";
            return false;
        }

        {
            std::unique_lock<std::mutex> lock(m_startMtx);
            m_startCondv.wait(lock, [&] { return m_readyWorkers == m_options.clients; });
            m_startTime = Clock::now();
            m_connectSetupSeconds =
                std::chrono::duration<double>(m_startTime - startupStarted).count();
            m_startWorkers = true;
        }
        m_startCondv.notify_all();
        return true;
    }

    void RunWorker(int workerId) {
        WorkerResult& result = m_results[static_cast<size_t>(workerId)];
        std::string connectionError;
        const int fd     = Connect(m_options, connectionError);
        result.connected = fd >= 0;
        if (!result.connected) result.error = connectionError;

        {
            std::lock_guard<std::mutex> lock(m_startMtx);
            ++m_readyWorkers;
        }
        m_startCondv.notify_one();
        {
            std::unique_lock<std::mutex> lock(m_startMtx);
            m_startCondv.wait(lock, [&] { return m_startWorkers; });
        }
        if (fd < 0) return;

        const auto finishTime = m_startTime + std::chrono::seconds(m_options.durationSeconds);
        while (Clock::now() < finishTime) {
            const uint64_t sequence    = m_requestSequence.fetch_add(1, std::memory_order_relaxed);
            const std::string username = "load_" + std::to_string(m_runNonce) + "_" +
                                         std::to_string(workerId) + "_" + std::to_string(sequence);
            const std::string phone    = FormatPhone(m_runNonce, sequence);
            uint64_t latencyUs         = 0;  // 延迟，微秒
            std::string requestError;
            ++result.attempted;
            if (SendRegisterAndReceive(fd, m_options, phone, username, latencyUs, requestError)) {
                ++result.succeeded;
                RecordLatency(result, latencyUs);
            } else {
                ++result.failed;
                if (result.error.empty()) result.error = requestError;
                // A timeout or broken stream may leave an unread response, so do not reuse it.
                if (requestError.find("rejected registration") != 0) break;
            }
        }
        close(fd);
    }

    int PrintSummary(Clock::time_point finishedTime) const {
        uint64_t attempted = 0;
        uint64_t succeeded = 0;
        uint64_t failed    = 0;
        uint64_t connected = 0;
        std::string firstError;
        std::array<uint64_t, 64> latencyHistogram{};
        uint64_t minLatencyUs = std::numeric_limits<uint64_t>::max();
        uint64_t maxLatencyUs = 0;
        for (const auto& result : m_results) {
            attempted += result.attempted;
            succeeded += result.succeeded;
            failed += result.failed;
            connected += result.connected ? 1 : 0;
            if (firstError.empty() && !result.error.empty()) firstError = result.error;
            for (size_t bucket = 0; bucket < latencyHistogram.size(); ++bucket) {
                latencyHistogram[bucket] += result.latencyHistogram[bucket];
            }
            if (result.succeeded > 0) {
                minLatencyUs = std::min(minLatencyUs, result.minLatencyUs);
                maxLatencyUs = std::max(maxLatencyUs, result.maxLatencyUs);
            }
        }
        const uint64_t latencyCount = succeeded;
        const double elapsedSeconds =
            std::chrono::duration<double>(finishedTime - m_startTime).count();
        const double requestsPerSecond = elapsedSeconds > 0 ? succeeded / elapsedSeconds : 0;
        const double failurePercent    = attempted > 0 ? 100.0 * failed / attempted : 0;

        std::cout << "register load test\n"
                  << "target=" << m_options.host << ':' << m_options.port
                  << " clients=" << m_options.clients
                  << " duration_sec=" << m_options.durationSeconds << "\n"
                  << "connected_clients=" << connected << '/' << m_options.clients
                  << " connect_setup_sec=" << m_connectSetupSeconds << "\n"
                  << "elapsed_sec=" << elapsedSeconds << " attempted=" << attempted
                  << " succeeded=" << succeeded << " failed=" << failed
                  << " failure_percent=" << failurePercent
                  << " successful_requests_per_sec=" << requestsPerSecond << "\n"
                  << "latency_ms_successful_only p50_upper="
                  << Percentile(latencyHistogram, latencyCount, 50) / 1000.0
                  << " p95_upper=" << Percentile(latencyHistogram, latencyCount, 95) / 1000.0
                  << " p99_upper=" << Percentile(latencyHistogram, latencyCount, 99) / 1000.0
                  << " min="
                  << (minLatencyUs == std::numeric_limits<uint64_t>::max() ? 0
                                                                           : minLatencyUs / 1000.0)
                  << " max=" << maxLatencyUs / 1000.0 << " samples=" << latencyCount << "\n";
        if (!firstError.empty()) std::cerr << "first_error=" << firstError << "\n";
        return failed == 0 && connected == static_cast<uint64_t>(m_options.clients) ? 0 : 1;
    }

    Options m_options;                           // 压测配置
    uint64_t m_runNonce;                         // 一次性随机数
    std::atomic<uint64_t> m_requestSequence{0};  // 所有线程共享的原子请求序号，避免分配重复序号
    std::vector<WorkerResult> m_results;         // 为每个 Worker(子线程) 单独保存统计结果
    std::vector<std::thread> m_workers;          // 创建出来的线程对象
    std::mutex m_startMtx;                       // 协调多个线程的启动时机
    std::condition_variable m_startCondv;        // 协调多个线程的启动时机
    int m_readyWorkers  = 0;                     // 记录就绪线程数量
    bool m_startWorkers = false;                 // 是否允许开始压测
    Clock::time_point m_startTime;               // 压测开始时间
    double m_connectSetupSeconds = 0;            // 所有子线程TCP连接建立耗时
};

int main(int argc, char** argv) {
    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--help" || std::string(argv[i]) == "-h") {
            PrintUsage(argv[0]);
            return 0;
        }
    }
    Options options;
    if (!ParseOptions(argc, argv, options)) return 2;
    RegisterLoadGenerator loadGenerator(std::move(options));
    return loadGenerator.Run();
}
