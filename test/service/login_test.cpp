#include <gtest/gtest.h>
#include <muduo/net/EventLoopThread.h>
#include <poll.h>

#include <atomic>
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
            auto conn = std::make_shared<muduo::net::TcpConnection>(
                loop, "service-integration-test", fd, endpoint, endpoint);
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

    void DisconnectPeer() {
        if (m_peerFd >= 0) {
            close(m_peerFd);
            m_peerFd = -1;
        }
    }

    bool HasReadableData(int timeoutMs) const {
        pollfd descriptor{};
        descriptor.fd     = m_peerFd;
        descriptor.events = POLLIN;
        const int ready   = poll(&descriptor, 1, timeoutMs);
        return ready > 0 && (descriptor.revents & POLLIN) != 0;
    }

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
            muduo::net::EventLoopThread::ThreadInitCallback(), "service-integration-test"));
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

    nlohmann::json Register(TestConnection& connection, const std::string& tag) {
        static std::atomic<uint64_t> sequence{0};
        const std::string unique = tag + "_" + std::to_string(sequence.fetch_add(1));
        return RegisterWithFields(connection, "139" + std::to_string(sequence.load()), unique,
                                  "service-test-password", "test-avatar");
    }

    nlohmann::json RegisterWithFields(TestConnection& connection, const std::string& phone,
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

    int64_t FindUserIdByName(const std::string& username) {
        MySqlDB db;
        if (!db.Connect()) return -1;
        MYSQL_RES* result = db.Query("select uid from im_user where nickname='" + username + "'");
        if (result == nullptr) return -1;
        MYSQL_ROW row        = mysql_fetch_row(result);
        const int64_t userId = row == nullptr ? -1 : std::stoll(row[0]);
        mysql_free_result(result);
        return userId;
    }

    std::string ReadStoredPassword(int64_t userId) {
        MySqlDB db;
        if (!db.Connect()) return {};
        MYSQL_RES* result =
            db.Query("select password from im_user where uid=" + std::to_string(userId));
        if (result == nullptr) return {};
        MYSQL_ROW row              = mysql_fetch_row(result);
        const std::string password = row == nullptr ? "" : row[0];
        mysql_free_result(result);
        return password;
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

    int64_t FriendRelationCount(int64_t userId, int64_t friendId) {
        MySqlDB db;
        if (!db.Connect()) return -1;
        MYSQL_RES* result =
            db.Query("select count(*) from im_friend where userid=" + std::to_string(userId) +
                     " and friendid=" + std::to_string(friendId));
        if (result == nullptr) return -1;
        MYSQL_ROW row       = mysql_fetch_row(result);
        const int64_t count = row == nullptr ? -1 : std::stoll(row[0]);
        mysql_free_result(result);
        return count;
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
    const auto registration = Register(*m_first, "service_login");
    ASSERT_EQ(registration.at("data").at("errtype").get<int>(),
              static_cast<int>(E_ERR_TYPE::REG_MSG_ACK_SUCCESS));
    const int64_t userId       = registration.at("data").at("userid").get<int64_t>();
    const std::string username = registration.at("data").at("username").get<std::string>();

    OfflineMsgModel offline;
    offline.Insert(userId, R"({"msgtype":9,"data":{"text":"queued"}})");

    const auto badPassword = Login(*m_first, username, "wrong-password");
    EXPECT_EQ(badPassword.at("data").at("errtype").get<int>(),
              static_cast<int>(E_ERR_TYPE::ACCOUNT_ERROR));

    const auto login = Login(*m_first, username, "service-test-password");
    ASSERT_EQ(login.at("data").at("errtype").get<int>(),
              static_cast<int>(E_ERR_TYPE::LOGIN_MSG_ACK_SUCCESS));
    ASSERT_TRUE(login.contains("offlinemsg"));
    EXPECT_EQ(login.at("offlinemsg").size(), 1u);
    EXPECT_EQ(offline.Query(userId).size(), 0u);
    EXPECT_EQ(UserChangeModel().QueryState(userId), "online");
    TrackLogin(userId, *m_first);

    const auto duplicateLogin = Login(*m_second, username, "service-test-password");
    EXPECT_EQ(duplicateLogin.at("data").at("errtype").get<int>(),
              static_cast<int>(E_ERR_TYPE::USER_ONLINE));
}

TEST_F(LoginIntegrationTest, LoginByPhoneUsesPhoneAccountType) {
    const auto registration = Register(*m_first, "service_phone_login");
    ASSERT_EQ(registration.at("data").at("errtype").get<int>(),
              static_cast<int>(E_ERR_TYPE::REG_MSG_ACK_SUCCESS));
    const int64_t userId = registration.at("data").at("userid").get<int64_t>();
    const auto phone     = registration.at("data").at("phone").get<std::string>();

    const auto login =
        LoginWithAccountType(*m_first, phone, "service-test-password", E_ACCOUNT_TYPE::PHONE);
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
    const auto registration = Register(*m_first, "service_concurrent_login");
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
        return Login(connection, username, "service-test-password");
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
    const auto registration = Register(*m_first, "service_disconnect");
    ASSERT_EQ(registration.at("data").at("errtype").get<int>(),
              static_cast<int>(E_ERR_TYPE::REG_MSG_ACK_SUCCESS));
    const auto userId   = registration.at("data").at("userid").get<int64_t>();
    const auto username = registration.at("data").at("username").get<std::string>();

    const auto login = Login(*m_first, username, "service-test-password");
    ASSERT_EQ(login.at("data").at("errtype").get<int>(),
              static_cast<int>(E_ERR_TYPE::LOGIN_MSG_ACK_SUCCESS));
    TrackLogin(userId, *m_first);
    ASSERT_EQ(UserChangeModel().QueryState(userId), "online");

    m_service->ClientCloseException(m_first->Connection());
    EXPECT_EQ(UserChangeModel().QueryState(userId), "offline");
}
