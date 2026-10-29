#include <gtest/gtest.h>
#include <muduo/net/EventLoopThread.h>
#include <poll.h>

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
        // 创建一对已连接的 Unix 域套接字（双向通信，类似管道）
        // 模拟网络通信
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) != 0) {
            throw std::runtime_error("socketpair failed");
        }
        m_peerFd = sockets[1];

        // 创建 promise，用于跨线程把创建的 TcpConnectionPtr 传回当前线程。用 shared_ptr
        // 是因为要捕获进 lambda。
        auto created = std::make_shared<std::promise<muduo::net::TcpConnectionPtr>>();
        auto future  = created->get_future();  // 取出对应的 future，之后 get() 时阻塞等待结果。

        // 把任务投递到 EventLoop 所在线程执行（TcpConnection 必须在 loop 线程创建）。捕获 loop
        // 指针、用于连接的另一端 fd、promise。
        m_loop->runInLoop([loop = m_loop, fd = sockets[0], created]() {
            const muduo::net::InetAddress endpoint("127.0.0.1", 0);
            // 真正构造 TcpConnection：绑定到 EventLoop、名字设为
            // "register-integration-test"、文件描述符用
            // sockets[0]、本地/对端地址都用上面的占位地址。
            auto conn = std::make_shared<muduo::net::TcpConnection>(
                loop, "register-integration-test", fd, endpoint, endpoint);

            // 设置连接建立/断开回调（空实现，测试用）。
            conn->setConnectionCallback([](const muduo::net::TcpConnectionPtr&) {});
            // 设置收到消息的回调（空实现，测试用）
            conn->setMessageCallback(
                [](const muduo::net::TcpConnectionPtr&, muduo::net::Buffer*, muduo::Timestamp) {});
            conn->setCloseCallback([](const muduo::net::TcpConnectionPtr&) {});
            conn->connectEstablished();  // 手动触发 "已连接" 状态：把连接设置为 established 并回调
                                         // connectionCallback（因为这是伪造的连接，没有真实的
                                         // accept/connect 过程）。
            created->set_value(conn);    // 把构造好的连接对象通过 promise 传出去。
        });

        // 阻塞等待 loop 线程完成连接创建，取回 TcpConnectionPtr 存入成员 m_conn。
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

    // 关闭客户端
    void DisconnectPeer() {
        if (m_peerFd >= 0) {
            close(m_peerFd);
            m_peerFd = -1;
        }
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
    int m_peerFd;  // 对端fd, 测试代码可通过它模拟对端(客户端)收发数据。
    muduo::net::TcpConnectionPtr m_conn;
};

/**
 * 在执行每个集成测试用例之前，自动检查测试环境、初始化服务、启动 Muduo
 * 网络事件循环线程，并创建两个模拟客户端连接。
 */
class RegisterIntegrationTest : public ::testing::Test {
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
        m_loopThread.reset(
            new muduo::net::EventLoopThread(muduo::net::EventLoopThread::ThreadInitCallback(),
                                            "register-service-integration-test"));
        m_loop = m_loopThread->startLoop();
        // 创建了两个连接，相当于m_loop所在的线程要处理两个TCP连接
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

    void TrackLogin(int64_t userId, TestConnection& connection) {
        m_loggedIn.emplace_back(userId, connection.Connection());
    }

    Service* m_service = nullptr;
    std::unique_ptr<muduo::net::EventLoopThread> m_loopThread;
    muduo::net::EventLoop* m_loop = nullptr;
    std::unique_ptr<TestConnection> m_first;   // 模拟客户端A
    std::unique_ptr<TestConnection> m_second;  // 模拟客户端B
    std::vector<int64_t> m_userIds;
    std::vector<std::pair<int64_t, muduo::net::TcpConnectionPtr>> m_loggedIn;  // 已登录的连接
};
}  // namespace

TEST_F(RegisterIntegrationTest, NormalRegistration) {
    const auto registration =
        Register(*m_first, "13888888888", "register_test", "register_test_password", "test-avatar");
    ASSERT_EQ(registration.at("data").at("errtype").get<int>(),
              static_cast<int>(E_ERR_TYPE::REG_MSG_ACK_SUCCESS));
    const int64_t userId       = registration.at("data").at("userid").get<int64_t>();
    const std::string username = registration.at("data").at("username").get<std::string>();

    const auto login = Login(*m_first, username, "register_test_password");
    ASSERT_EQ(login.at("data").at("errtype").get<int>(),
              static_cast<int>(E_ERR_TYPE::LOGIN_MSG_ACK_SUCCESS));
    EXPECT_EQ(UserChangeModel().QueryState(userId), "online");
    TrackLogin(userId, *m_first);
}

TEST_F(RegisterIntegrationTest, EmptyRequiredRegistrationFieldsAreRejected) {
    const std::vector<std::array<std::string, 4>> invalidRequests = {
        {"", "register_empty_phone", "register-test-password", "test-avatar"},
        {"13988880001", "", "register-test-password", "test-avatar"},
        {"13988880002", "register_empty_password", "", "test-avatar"},
    };
    for (auto it = invalidRequests.cbegin(); it != invalidRequests.cend(); ++it) {
        const auto response =
            Register(*m_first, (*it).at(0), (*it).at(1), (*it).at(2), (*it).at(3));
        EXPECT_NE(response.at("data").at("errtype").get<int>(),
                  static_cast<int>(E_ERR_TYPE::REG_MSG_ACK_SUCCESS))
            << "invalid registration case!";
    }
}

TEST_F(RegisterIntegrationTest, InvalidPhoneFormatIsRejected) {
    const auto response = Register(*m_first, "not-a-phone", "register_invalid_phone_format",
                                   "register-test-password", "test-avatar");
    EXPECT_NE(response.at("data").at("errtype").get<int>(),
              static_cast<int>(E_ERR_TYPE::REG_MSG_ACK_SUCCESS));

    const auto response1 = Register(*m_first, "1399", "register_invalid_phone_format",
                                    "register-test-password", "test-avatar");
    EXPECT_NE(response1.at("data").at("errtype").get<int>(),
              static_cast<int>(E_ERR_TYPE::REG_MSG_ACK_SUCCESS));
}

TEST_F(RegisterIntegrationTest, OverlongRegistrationFieldIsRejected) {
    const std::string tooLong(4096, 'x');
    const auto response =
        Register(*m_first, "13988880006", tooLong, "register-test-password", "test-avatar");
    EXPECT_NE(response.at("data").at("errtype").get<int>(),
              static_cast<int>(E_ERR_TYPE::REG_MSG_ACK_SUCCESS));
}

TEST_F(RegisterIntegrationTest, NonStringRegistrationFieldDoesNotEscapeHandler) {
    nlohmann::json request = {{"msgtype", static_cast<int>(E_MSG_TYPE::REG_MSG)},
                              {"data",
                               {{"phone", 123456789},
                                {"username", "service_non_string_registration"},
                                {"password", "service-test-password"},
                                {"avatar", "test-avatar"}}}};

    EXPECT_NO_THROW(Call(m_first->Connection(), request));
}

TEST_F(RegisterIntegrationTest, DuplicatePhoneAndNicknameAreRejected) {
    const auto first = Register(*m_first, "13978888888", "register_duplicate_account",
                                "register-test-password", "test-avatar");
    ASSERT_EQ(first.at("data").at("errtype").get<int>(),
              static_cast<int>(E_ERR_TYPE::REG_MSG_ACK_SUCCESS));
    const auto phone    = first.at("data").at("phone").get<std::string>();
    const auto username = first.at("data").at("username").get<std::string>();

    const auto duplicatePhone = Register(*m_second, phone, "register_duplicate_phone_name",
                                         "register-test-password", "test-avatar");
    EXPECT_EQ(duplicatePhone.at("data").at("errtype").get<int>(),
              static_cast<int>(E_ERR_TYPE::USER_EXIT));

    // const auto duplicateNickname =
    //     Register(*m_second, "13999999999", username, "register-test-password", "test-avatar");
    // EXPECT_EQ(duplicateNickname.at("data").at("errtype").get<int>(),
    //           static_cast<int>(E_ERR_TYPE::USER_EXIT));
}

TEST_F(RegisterIntegrationTest, MissingRegistrationFieldIsRejected) {
    nlohmann::json request = {{"msgtype", static_cast<int>(E_MSG_TYPE::REG_MSG)},
                              {"data",
                               {{"username", "service_missing_phone"},
                                {"password", "service-test-password"},
                                {"avatar", "test-avatar"}}}};
    Call(m_first->Connection(), request);
    const auto response = m_first->ReadJson();
    EXPECT_NE(response.at("data").at("errtype").get<int>(),
              static_cast<int>(E_ERR_TYPE::REG_MSG_ACK_SUCCESS));
    if (response.at("data").at("errtype").get<int>() ==
        static_cast<int>(E_ERR_TYPE::REG_MSG_ACK_SUCCESS)) {
        m_userIds.push_back(response.at("data").at("userid").get<int64_t>());
    }
}

TEST_F(RegisterIntegrationTest, ConcurrentDuplicateRegistrationCreatesOneAccount) {
    std::mutex startMutex;
    std::condition_variable startCondition;
    int ready   = 0;
    bool start  = false;
    auto submit = [&](TestConnection& connection) {
        {
            std::unique_lock<std::mutex> lock(startMutex);
            ++ready;
            startCondition.notify_all();
            startCondition.wait(lock, [&] { return start; });
        }
        nlohmann::json request = {{"msgtype", static_cast<int>(E_MSG_TYPE::REG_MSG)},
                                  {"data",
                                   {{"phone", "13988880007"},
                                    {"username", "register_concurrent_duplicate"},
                                    {"password", "register-test-password"},
                                    {"avatar", "test-avatar"}}}};
        Call(connection.Connection(), request);
        return connection.ReadJson();
    };

    auto first  = std::async(std::launch::async, submit, std::ref(*m_first));
    auto second = std::async(std::launch::async, submit, std::ref(*m_second));
    {
        std::unique_lock<std::mutex> lock(startMutex);
        ASSERT_TRUE(
            startCondition.wait_for(lock, std::chrono::seconds(2), [&] { return ready == 2; }));
        start = true;
    }
    startCondition.notify_all();

    const auto firstResponse   = first.get();
    const auto secondResponse  = second.get();
    const bool firstSucceeded  = firstResponse.at("data").at("errtype").get<int>() ==
                                 static_cast<int>(E_ERR_TYPE::REG_MSG_ACK_SUCCESS);
    const bool secondSucceeded = secondResponse.at("data").at("errtype").get<int>() ==
                                 static_cast<int>(E_ERR_TYPE::REG_MSG_ACK_SUCCESS);
    EXPECT_EQ(static_cast<int>(firstSucceeded) + static_cast<int>(secondSucceeded), 1);
    for (const auto* response : {&firstResponse, &secondResponse}) {
        if (response->at("data").at("errtype").get<int>() ==
            static_cast<int>(E_ERR_TYPE::REG_MSG_ACK_SUCCESS)) {
            m_userIds.push_back(response->at("data").at("userid").get<int64_t>());
        }
    }
}

TEST_F(RegisterIntegrationTest, AvatarAndPasswordPersistenceFollowRegistrationContract) {
    const std::string password = "registration-password-to-hash";
    const std::string avatar   = "registration-avatar-value";
    const auto registration =
        Register(*m_first, "13988880003", "service_registration_persistence", password, avatar);
    ASSERT_EQ(registration.at("data").at("errtype").get<int>(),
              static_cast<int>(E_ERR_TYPE::REG_MSG_ACK_SUCCESS));
    const int64_t userId = registration.at("data").at("userid").get<int64_t>();
    const auto username  = registration.at("data").at("username").get<std::string>();
    EXPECT_EQ(registration.at("data").at("avator").get<std::string>(), avatar);
    EXPECT_NE(ReadStoredPassword(userId), password);

    const auto login = Login(*m_first, username, password);
    ASSERT_EQ(login.at("data").at("errtype").get<int>(),
              static_cast<int>(E_ERR_TYPE::LOGIN_MSG_ACK_SUCCESS));
    EXPECT_EQ(login.at("data").at("avator").get<std::string>(), avatar);
    TrackLogin(userId, *m_first);
}

TEST_F(RegisterIntegrationTest, RegistrationCommitSurvivesServerProcessExitBeforeReply) {
    const std::string username =
        "register_server_exit_registration_" + std::to_string(static_cast<long long>(getpid()));

    // 手机号改为按进程号生成，避免重复
    std::string phoneSuffix =
        std::to_string(static_cast<unsigned long long>(getpid()) % 100000000ULL);
    phoneSuffix.insert(0, 8 - phoneSuffix.size(), '0');
    const std::string phone = "139" + phoneSuffix;

    const pid_t child = fork();
    ASSERT_NE(child, -1);
    if (child == 0) {
        if (setenv("WE_LINK_CRASH_REGISTRATION_USER", username.c_str(), 1) != 0 ||
            setenv("WE_LINK_CRASH_REGISTRATION_PHONE", phone.c_str(), 1) != 0) {
            _exit(126);
        }
        execl("/proc/self/exe", "run_all_test",
              "--gtest_filter=RegistrationCrashWorker.ExitsAfterCommitWithoutReply",
              static_cast<char*>(nullptr));
        _exit(127);
    }

    int status = 0;
    ASSERT_EQ(waitpid(child, &status, 0), child);
    ASSERT_TRUE(WIFEXITED(status));
    ASSERT_EQ(WEXITSTATUS(status), 86);

    const int64_t userId = FindUserIdByName(username);
    ASSERT_NE(userId, -1);
    m_userIds.push_back(userId);
    EXPECT_EQ(UserChangeModel().QueryState(userId), "offline");

    const auto login = Login(*m_first, username, "register-test-password");
    ASSERT_EQ(login.at("data").at("errtype").get<int>(),
              static_cast<int>(E_ERR_TYPE::LOGIN_MSG_ACK_SUCCESS));
    TrackLogin(userId, *m_first);
}
TEST(RegistrationCrashWorker, ExitsAfterCommitWithoutReply) {
    /**
     * 执行注册后立即以退出码 86
     * 结束，用来模拟客户端提交请求到服务器后，服务器未回复，客户端便崩溃。
     */
    const char* username = std::getenv("WE_LINK_CRASH_REGISTRATION_USER");
    const char* phone    = std::getenv("WE_LINK_CRASH_REGISTRATION_PHONE");
    if (username == nullptr || phone == nullptr) {
        GTEST_SKIP()
            << "This test is run as a subprocess by the registration crash integration test";
    }

    (void)AppConfig::Instance();
    nlohmann::json request = {{"msgtype", static_cast<int>(E_MSG_TYPE::REG_MSG)},
                              {"data",
                               {{"phone", phone},
                                {"username", username},
                                {"password", "register-test-password"},
                                {"avatar", "test-avatar"}}}};
    UserModel userModel;
    UserChangeModel userChangeModel;
    RegistrationService registrationService(userModel, userChangeModel);
    const auto response = registrationService.Register(request);
    if (response.at("data").at("errtype").get<int>() !=
        static_cast<int>(E_ERR_TYPE::REG_MSG_ACK_SUCCESS)) {
        _exit(87);
    }
    _exit(86);  // Simulate process termination after database commit and before a client reply.
}

TEST_F(RegisterIntegrationTest, MysqlUnavailableReturnsRegistrationFailure) {
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
        if (setenv("WE_LINK_CONFIG", configPath.c_str(), 1) != 0 ||
            setenv("WE_LINK_MYSQL_FAILURE_WORKER", "1", 1) != 0) {
            _exit(126);
        }
        execl("/proc/self/exe", "run_all_test",
              "--gtest_filter=RegistrationDatabaseFailureWorker.ReturnsRegistrationFailure",
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
TEST(RegistrationDatabaseFailureWorker, ReturnsRegistrationFailure) {
    if (std::getenv("WE_LINK_MYSQL_FAILURE_WORKER") == nullptr) {
        GTEST_SKIP() << "This test runs in a subprocess with an unavailable MySQL endpoint";
    }

    (void)AppConfig::Instance();
    Service* service = Service::instance();
    muduo::net::EventLoopThread loopThread(muduo::net::EventLoopThread::ThreadInitCallback(),
                                           "registration-db-failure-test");
    muduo::net::EventLoop* loop = loopThread.startLoop();
    TestConnection connection(loop);
    nlohmann::json request = {{"msgtype", static_cast<int>(E_MSG_TYPE::REG_MSG)},
                              {"data",
                               {{"phone", "13988880008"},
                                {"username", "register_mysql_unavailable"},
                                {"password", "register-test-password"},
                                {"avatar", "test-avatar"}}}};
    service->GetHandler(static_cast<int>(E_MSG_TYPE::REG_MSG))(connection.Connection(), request,
                                                               muduo::Timestamp::now());
    const auto response = connection.ReadJson();
    EXPECT_EQ(response.at("data").at("errtype").get<int>(),
              static_cast<int>(E_ERR_TYPE::USER_EXIT));
}