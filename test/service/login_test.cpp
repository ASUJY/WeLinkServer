#include <gtest/gtest.h>
#include <muduo/net/EventLoopThread.h>
#include <poll.h>

#include <atomic>
#include <fstream>
#include <future>

#include "config.h"
#include "json.hpp"
#include "mysqldb.h"
#include "server.h"
#include "service.h"

namespace {
constexpr int messageMaxLen = 1024 * 1024;

bool IntegrationEnabled() {
    const char* enabled = std::getenv("WE_LINK_SERVICE_INTEGRATION");
    return enabled != nullptr && std::string(enabled) == "1";
}

bool ReadExact(int fd, char* data, size_t size) {
    size_t received = 0;
    while (received < size) {
        pollfd descriptor{};
        descriptor.fd     = fd;
        descriptor.events = POLLIN;
        const int ready   = poll(&descriptor, 1, 5000);
        if (ready <= 0 || (descriptor.revents & (POLLERR | POLLHUP | POLLNVAL))) {
            return false;
        }
        const ssize_t count = recv(fd, data + received, size - received, 0);
        if (count <= 0) {
            if (count < 0 && errno == EINTR) continue;
            return false;
        }
        received += static_cast<size_t>(count);
    }
    return true;
}

class TestConnection {
public:
    explicit TestConnection(muduo::net::EventLoop* loop) : m_loop(loop), m_peerFd(-1) {
        int sockets[2];
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) != 0) {
            throw std::runtime_error("socketpair failed");
        }
        m_peerFd = sockets[1];

        auto created = std::make_shared<std::promise<muduo::net::TcpConnectionPtr>>();
        auto future  = created->get_future();
        m_loop->runInLoop([loop = m_loop, fd = sockets[0], created]() {
            const muduo::net::InetAddress endpoint("127.0.0.1", 0);
            auto conn = std::make_shared<muduo::net::TcpConnection>(loop, "login-integration-test",
                                                                    fd, endpoint, endpoint);
            conn->setConnectionCallback([](const muduo::net::TcpConnectionPtr&) {});
            conn->setMessageCallback(
                [](const muduo::net::TcpConnectionPtr&, muduo::net::Buffer*, muduo::Timestamp) {});
            conn->connectEstablished();
            created->set_value(conn);
        });
        m_conn = future.get();
    }

    ~TestConnection() {
        if (m_conn) {
            auto destroyed = std::make_shared<std::promise<void>>();
            auto future    = destroyed->get_future();
            auto conn      = m_conn;
            m_loop->runInLoop([conn, destroyed]() {
                conn->connectDestroyed();
                destroyed->set_value();
            });
            future.wait();
            m_conn.reset();
        }
        if (m_peerFd >= 0) close(m_peerFd);
    }

    const muduo::net::TcpConnectionPtr& Connection() const { return m_conn; }

    // void DisconnectPeer() {
    //     if (m_peerFd >= 0) {
    //         close(m_peerFd);
    //         m_peerFd = -1;
    //     }
    // }

    nlohmann::json ReadJson() const {
        uint32_t networkLength = 0;
        if (!ReadExact(m_peerFd, reinterpret_cast<char*>(&networkLength), sizeof(networkLength))) {
            throw std::runtime_error("timed out waiting for service response header");
        }
        const uint32_t length = ntohl(networkLength);
        if (length == 0 || length > messageMaxLen) {
            throw std::runtime_error("service returned an invalid frame length");
        }
        std::string body(length, '\0');
        if (!ReadExact(m_peerFd, &body[0], body.size())) {
            throw std::runtime_error("timed out waiting for service response body");
        }
        return nlohmann::json::parse(body);
    }

private:
    muduo::net::EventLoop* m_loop;
    int m_peerFd;
    muduo::net::TcpConnectionPtr m_conn;
};

class LoginIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        if (!IntegrationEnabled()) {
            GTEST_SKIP()
                << "Set WE_LINK_SERVICE_INTEGRATION=1 with local MySQL and Redis configured";
        }
        // Fail early with a useful configuration error instead of terminating
        // inside Service/Redis construction.
        (void)AppConfig::Instance();
        m_service = Service::instance();
        m_loopThread.reset(new muduo::net::EventLoopThread(
            muduo::net::EventLoopThread::ThreadInitCallback(), "login-service-integration-test"));
        m_loop = m_loopThread->startLoop();
        m_first.reset(new TestConnection(m_loop));
        m_second.reset(new TestConnection(m_loop));
    }

    void TearDown() override {
        if (m_service) {
            for (const auto& loggedIn : m_loggedIn) {
                m_service->ClientCloseException(loggedIn.second);
            }
        }
        for (int64_t userId : m_userIds) {
            OfflineMsgModel().Remove(userId);
            UserChangeModel().UpdateState(userId, "offline");
            MySqlDB db;
            if (db.Connect()) {
                db.Update("delete from im_friend where userid=" + std::to_string(userId) +
                          " or friendid=" + std::to_string(userId));
                db.Update("delete from im_userchange where userid=" + std::to_string(userId));
                db.Update("delete from im_user where uid=" + std::to_string(userId));
            }
        }
        m_first.reset();
        m_second.reset();
        m_loopThread.reset();
    }

    nlohmann::json Call(const muduo::net::TcpConnectionPtr& conn, nlohmann::json request) {
        const int type = request.at("msgtype").get<int>();
        m_service->GetHandler(type)(conn, request, muduo::Timestamp::now());
        return request;
    }

    // nlohmann::json Register(TestConnection& connection, const std::string& tag) {
    //     static std::atomic<uint64_t> sequence{0};
    //     const std::string unique = tag + "_" + std::to_string(sequence.fetch_add(1));
    //     return RegisterWithFields(connection, "139" + std::to_string(sequence.load()), unique,
    //                               "service-test-password", "test-avatar");
    // }

    nlohmann::json Register(TestConnection& connection, const std::string& phone,
                            const std::string& username, const std::string& password,
                            const std::string& avatar) {
        nlohmann::json request = {{"msgtype", static_cast<int>(E_MSG_TYPE::REG_MSG)},
                                  {"data",
                                   {{"phone", phone},
                                    {"username", username},
                                    {"password", password},
                                    {"avatar", avatar}}}};
        Call(connection.Connection(), request);
        nlohmann::json response = connection.ReadJson();
        if (response.at("data").at("errtype").get<int>() ==
            static_cast<int>(E_ERR_TYPE::REG_MSG_ACK_SUCCESS)) {
            m_userIds.push_back(response.at("data").at("userid").get<int64_t>());
        }
        return response;
    }

    nlohmann::json Login(TestConnection& connection, const std::string& username,
                         const std::string& password) {
        return LoginWithAccountType(connection, username, password, E_ACCOUNT_TYPE::NICKNAME);
    }

    nlohmann::json LoginWithAccountType(TestConnection& connection, const std::string& account,
                                        const std::string& password, E_ACCOUNT_TYPE accountType) {
        nlohmann::json request = {
            {"msgtype", static_cast<int>(E_MSG_TYPE::LOGIN_MSG)},
            {"data",
             {{"username", account},
              {"password", password},
              {"accountType", std::to_string(static_cast<int>(accountType))}}}};
        Call(connection.Connection(), request);
        return connection.ReadJson();
    }

    void TrackLogin(int64_t userId, TestConnection& connection) {
        m_loggedIn.emplace_back(userId, connection.Connection());
    }

    Service* m_service = nullptr;
    std::unique_ptr<muduo::net::EventLoopThread> m_loopThread;
    muduo::net::EventLoop* m_loop = nullptr;
    std::unique_ptr<TestConnection> m_first;
    std::unique_ptr<TestConnection> m_second;
    std::vector<int64_t> m_userIds;
    std::vector<std::pair<int64_t, muduo::net::TcpConnectionPtr>> m_loggedIn;
};
}  // namespace

TEST_F(LoginIntegrationTest, RegistrationLoginAndOfflineMessages) {
    const auto registration =
        Register(*m_first, "13188888888", "service_login", "login-test-password", "test-avatar");
    ASSERT_EQ(registration.at("data").at("errtype").get<int>(),
              static_cast<int>(E_ERR_TYPE::REG_MSG_ACK_SUCCESS));
    const int64_t userId       = registration.at("data").at("userid").get<int64_t>();
    const std::string username = registration.at("data").at("username").get<std::string>();

    OfflineMsgModel offline;
    offline.Insert(userId, R"({"msgtype":9,"data":{"text":"queued"}})");

    // 错误密码拒绝登录
    const auto badPassword = Login(*m_first, username, "wrong-password");
    EXPECT_EQ(badPassword.at("data").at("errtype").get<int>(),
              static_cast<int>(E_ERR_TYPE::ACCOUNT_ERROR));

    // 用户名和正确密码，登录成功，用户状态变为在线状态
    // 登录成功，如果有离线消息，会收到离线消息，离线消息随后会从数据库清除
    const auto login = Login(*m_first, username, "login-test-password");
    ASSERT_EQ(login.at("data").at("errtype").get<int>(),
              static_cast<int>(E_ERR_TYPE::LOGIN_MSG_ACK_SUCCESS));
    ASSERT_TRUE(login.contains("offlinemsg"));
    EXPECT_EQ(login.at("offlinemsg").size(), 1u);
    EXPECT_EQ(offline.Query(userId).size(), 0u);
    EXPECT_EQ(UserChangeModel().QueryState(userId), "online");
    TrackLogin(userId, *m_first);

    // 同一用户再次登录会被拒绝
    const auto duplicateLogin = Login(*m_second, username, "login-test-password");
    EXPECT_EQ(duplicateLogin.at("data").at("errtype").get<int>(),
              static_cast<int>(E_ERR_TYPE::USER_ONLINE));
}

TEST_F(LoginIntegrationTest, LoginByPhoneUsesPhoneAccountType) {
    const auto registration = Register(*m_first, "13111111111", "service_phone_login",
                                       "login-test-password", "test-avatar");
    ASSERT_EQ(registration.at("data").at("errtype").get<int>(),
              static_cast<int>(E_ERR_TYPE::REG_MSG_ACK_SUCCESS));
    const int64_t userId = registration.at("data").at("userid").get<int64_t>();
    const auto phone     = registration.at("data").at("phone").get<std::string>();

    const auto login =
        LoginWithAccountType(*m_first, phone, "login-test-password", E_ACCOUNT_TYPE::PHONE);
    ASSERT_EQ(login.at("data").at("errtype").get<int>(),
              static_cast<int>(E_ERR_TYPE::LOGIN_MSG_ACK_SUCCESS));
    EXPECT_EQ(login.at("data").at("userid").get<int64_t>(), userId);
    TrackLogin(userId, *m_first);
}

TEST_F(LoginIntegrationTest, MalformedLoginAccountTypeDoesNotEscapeHandler) {
    nlohmann::json malformedLogin = {{"msgtype", static_cast<int>(E_MSG_TYPE::LOGIN_MSG)},
                                     {"data",
                                      {{"username", "missing-user"},
                                       {"password", "any-password"},
                                       {"accountType", "not-a-number"}}}};

    EXPECT_NO_THROW(Call(m_first->Connection(), malformedLogin));
}

TEST_F(LoginIntegrationTest, ConcurrentLoginAllowsOnlyOneConnection) {
    const auto registration = Register(*m_first, "13222222222", "login_service_concurrent_login",
                                       "login-test-password", "test-avatar");
    ASSERT_EQ(registration.at("data").at("errtype").get<int>(),
              static_cast<int>(E_ERR_TYPE::REG_MSG_ACK_SUCCESS));
    const auto username = registration.at("data").at("username").get<std::string>();
    const auto userId   = registration.at("data").at("userid").get<int64_t>();

    std::mutex startMutex;
    std::condition_variable startCondition;
    int ready           = 0;
    bool start          = false;
    auto loginAtBarrier = [&](TestConnection& connection) {
        {
            std::unique_lock<std::mutex> lock(startMutex);
            ++ready;
            startCondition.notify_all();
            startCondition.wait(lock, [&] { return start; });
        }
        return Login(connection, username, "login-test-password");
    };

    auto firstResult  = std::async(std::launch::async, loginAtBarrier, std::ref(*m_first));
    auto secondResult = std::async(std::launch::async, loginAtBarrier, std::ref(*m_second));
    bool bothReady    = false;
    {
        std::unique_lock<std::mutex> lock(startMutex);
        bothReady =
            startCondition.wait_for(lock, std::chrono::seconds(2), [&] { return ready == 2; });
        start = true;
    }
    startCondition.notify_all();
    ASSERT_TRUE(bothReady);

    const auto first      = firstResult.get();
    const auto second     = secondResult.get();
    const int firstError  = first.at("data").at("errtype").get<int>();
    const int secondError = second.at("data").at("errtype").get<int>();
    const int successes   = (firstError == static_cast<int>(E_ERR_TYPE::LOGIN_MSG_ACK_SUCCESS)) +
                            (secondError == static_cast<int>(E_ERR_TYPE::LOGIN_MSG_ACK_SUCCESS));
    EXPECT_EQ(successes, 1);
    EXPECT_TRUE(firstError == static_cast<int>(E_ERR_TYPE::USER_ONLINE) ||
                secondError == static_cast<int>(E_ERR_TYPE::USER_ONLINE));
    EXPECT_EQ(UserChangeModel().QueryState(userId), "online");
    if (firstError == static_cast<int>(E_ERR_TYPE::LOGIN_MSG_ACK_SUCCESS)) {
        TrackLogin(userId, *m_first);
    } else if (secondError == static_cast<int>(E_ERR_TYPE::LOGIN_MSG_ACK_SUCCESS)) {
        TrackLogin(userId, *m_second);
    }
}

TEST_F(LoginIntegrationTest, ClosingLoggedInConnectionMarksUserOffline) {
    const auto registration = Register(*m_first, "13333333333", "login_service_disconnect",
                                       "login-test-password", "test-avatar");
    ASSERT_EQ(registration.at("data").at("errtype").get<int>(),
              static_cast<int>(E_ERR_TYPE::REG_MSG_ACK_SUCCESS));
    const auto userId   = registration.at("data").at("userid").get<int64_t>();
    const auto username = registration.at("data").at("username").get<std::string>();

    const auto login = Login(*m_first, username, "login-test-password");
    ASSERT_EQ(login.at("data").at("errtype").get<int>(),
              static_cast<int>(E_ERR_TYPE::LOGIN_MSG_ACK_SUCCESS));
    TrackLogin(userId, *m_first);
    ASSERT_EQ(UserChangeModel().QueryState(userId), "online");

    m_service->ClientCloseException(m_first->Connection());
    EXPECT_EQ(UserChangeModel().QueryState(userId), "offline");
}

TEST_F(LoginIntegrationTest, MysqlUnavailableReturnsLoginFailure) {
    const std::string configPath = "/tmp/welink_mysql_unavailable_" +
                                   std::to_string(static_cast<long long>(getpid())) + ".json";

    {
        std::ofstream config(configPath);
        ASSERT_TRUE(config.good());
        config << nlohmann::json{{"mysql",
                                  {{"host", "127.0.0.1"},
                                   {"port", 1},
                                   {"user", "missing"},
                                   {"password", ""},
                                   {"database", "missing"},
                                   {"charset", "utf8mb4"}}},
                                 {"redis", {{"host", "127.0.0.1"}, {"port", 1}, {"password", ""}}}};
    }

    const pid_t child = fork();
    ASSERT_NE(child, -1);
    if (child == 0) {
        // child process
        if (setenv("WE_LINK_CONFIG", configPath.c_str(), 1) != 0 ||
            setenv("WE_LINK_MYSQL_FAILURE_WORKER", "1", 1) != 0) {
            _exit(126);
        }
        execl("/proc/self/exe", "run_all_test",
              "--gtest_filter=LoginDatabaseFailureWorker.ReturnsloginFailure",
              static_cast<char*>(nullptr));
        _exit(127);
    }

    int status         = 0;
    const pid_t waited = waitpid(child, &status, 0);
    std::remove(configPath.c_str());
    ASSERT_EQ(waited, child);
    ASSERT_TRUE(WIFEXITED(status));
    EXPECT_EQ(WEXITSTATUS(status), 0);
}

TEST(LoginDatabaseFailureWorker, ReturnsloginFailure) {
    if (std::getenv("WE_LINK_MYSQL_FAILURE_WORKER") == nullptr) {
        GTEST_SKIP() << "This test runs in a subprocess with an unavailable MySQL endpoint";
    }

    (void)AppConfig::Instance();
    Service* service = Service::instance();
    muduo::net::EventLoopThread loopThread(muduo::net::EventLoopThread::ThreadInitCallback(),
                                           "login-db-failure-test");
    muduo::net::EventLoop* loop = loopThread.startLoop();
    TestConnection connection(loop);
    nlohmann::json request = {
        {"msgtype", static_cast<int>(E_MSG_TYPE::LOGIN_MSG)},
        {"data",
         {{"username", "login_mysql_unavailable"},
          {"password", "login-test-password"},
          {"accountType", std::to_string(static_cast<int>(E_ACCOUNT_TYPE::NICKNAME))}}}};
    service->GetHandler(static_cast<int>(E_MSG_TYPE::LOGIN_MSG))(connection.Connection(), request,
                                                                 muduo::Timestamp::now());
    const auto response = connection.ReadJson();
    EXPECT_EQ(response.at("data").at("errtype").get<int>(),
              static_cast<int>(E_ERR_TYPE::USER_NO_REGISTER));
}