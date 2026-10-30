#include <gtest/gtest.h>
#include <muduo/net/EventLoopThread.h>
#include <poll.h>

#include <future>

#include "config.h"
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

    // void DisconnectPeer() {
    //     if (m_peerFd >= 0) {
    //         close(m_peerFd);
    //         m_peerFd = -1;
    //     }
    // }

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

class OneChatIntegrationTest : public ::testing::Test {
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

TEST_F(OneChatIntegrationTest, OnlineUserNotFriendNoSendMsg) {
    // 登录注册sender
    const auto sender =
        Register(*m_first, "13311111111", "service_sender", "onechat-test-password", "test-avatar");
    int64_t senderId             = sender.at("data").at("userid").get<int64_t>();
    const std::string senderName = sender.at("data").at("username").get<std::string>();
    const auto senderLogin       = Login(*m_first, senderName, "onechat-test-password");
    ASSERT_EQ(senderLogin.at("data").at("errtype").get<int>(),
              static_cast<int>(E_ERR_TYPE::LOGIN_MSG_ACK_SUCCESS));
    TrackLogin(senderId, *m_first);

    // 登录注册receiver
    const auto receiver            = Register(*m_second, "13333333333", "service_receiver",
                                              "onechat-test-password", "test-avatar");
    int64_t receiverId             = receiver.at("data").at("userid").get<int64_t>();
    const std::string receiverName = receiver.at("data").at("username").get<std::string>();
    const auto receiverLogin       = Login(*m_second, receiverName, "onechat-test-password");
    ASSERT_EQ(receiver.at("data").at("errtype").get<int>(),
              static_cast<int>(E_ERR_TYPE::REG_MSG_ACK_SUCCESS));
    TrackLogin(receiverId, *m_second);

    // sender 向 receiver发送消息
    // 未添加为好友的时候不能发送信息给对方
    nlohmann::json receiverChat = {
        {"msgtype", static_cast<int>(E_MSG_TYPE::ONE_CHAT_MSG)},
        {"data", {{"receiverId", receiverId}, {"text", "online chat payload"}}}};
    Call(m_first->Connection(), receiverChat);
    EXPECT_THROW(m_second->ReadJson(), std::runtime_error);
}

TEST_F(OneChatIntegrationTest, OfflineUserNotFriendNoSendMsg) {
    // 登录注册sender
    const auto sender =
        Register(*m_first, "13311111111", "service_sender", "onechat-test-password", "test-avatar");
    int64_t senderId             = sender.at("data").at("userid").get<int64_t>();
    const std::string senderName = sender.at("data").at("username").get<std::string>();
    const auto senderLogin       = Login(*m_first, senderName, "onechat-test-password");
    ASSERT_EQ(senderLogin.at("data").at("errtype").get<int>(),
              static_cast<int>(E_ERR_TYPE::LOGIN_MSG_ACK_SUCCESS));
    TrackLogin(senderId, *m_first);

    // 注册offline，测试离线用户
    const auto offline = Register(*m_second, "13333333333", "service_offline",
                                  "onechat-test-password", "test-avatar");
    ASSERT_EQ(offline.at("data").at("errtype").get<int>(),
              static_cast<int>(E_ERR_TYPE::REG_MSG_ACK_SUCCESS));
    int64_t offlineId             = offline.at("data").at("userid").get<int64_t>();
    const std::string offlineName = offline.at("data").at("username").get<std::string>();

    nlohmann::json offlineChat = {
        {"msgtype", static_cast<int>(E_MSG_TYPE::ONE_CHAT_MSG)},
        {"data", {{"receiverId", offlineId}, {"text", "offline payload"}}}};
    Call(m_first->Connection(), offlineChat);

    const auto queued = OfflineMsgModel().Query(offlineId);
    for (auto it = queued.begin(); it != queued.end(); ++it) {
        std::cout << "Offlinemessage: " << *it << std::endl;
    }
    ASSERT_EQ(queued.size(), 0);
    bool foundChat = false;
    for (const auto& message : queued) {
        const auto queuedJson = nlohmann::json::parse(message);
        const int messageType = queuedJson.at("msgtype").get<int>();
        if (messageType == static_cast<int>(E_MSG_TYPE::ONE_CHAT_MSG)) {
            foundChat = queuedJson.at("data").at("text").get<std::string>() == "offline payload";
        }
    }
    EXPECT_FALSE(foundChat);
}

TEST_F(OneChatIntegrationTest, OfflineUserIsFriendSendMsg) {
    const auto sender =
        Register(*m_first, "13311111111", "service_sender", "onechat-test-password", "test-avatar");
    const auto receiver = Register(*m_second, "13322222222", "service_receiver",
                                   "onechat-test-password", "test-avatar");
    ASSERT_EQ(sender.at("data").at("errtype").get<int>(),
              static_cast<int>(E_ERR_TYPE::REG_MSG_ACK_SUCCESS));
    ASSERT_EQ(receiver.at("data").at("errtype").get<int>(),
              static_cast<int>(E_ERR_TYPE::REG_MSG_ACK_SUCCESS));
    int64_t senderId               = sender.at("data").at("userid").get<int64_t>();
    int64_t receiverId             = receiver.at("data").at("userid").get<int64_t>();
    const std::string senderName   = sender.at("data").at("username").get<std::string>();
    const std::string receiverName = receiver.at("data").at("username").get<std::string>();

    const auto senderLogin = Login(*m_first, senderName, "onechat-test-password");
    ASSERT_EQ(senderLogin.at("data").at("errtype").get<int>(),
              static_cast<int>(E_ERR_TYPE::LOGIN_MSG_ACK_SUCCESS));
    TrackLogin(senderId, *m_first);

    const auto receiverLogin = Login(*m_second, receiverName, "onechat-test-password");
    ASSERT_EQ(receiverLogin.at("data").at("errtype").get<int>(),
              static_cast<int>(E_ERR_TYPE::LOGIN_MSG_ACK_SUCCESS));
    TrackLogin(receiverId, *m_second);

    // sender用户向receiver用户发起添加好友申请
    nlohmann::json friendRequest = {{"msgtype", static_cast<int>(E_MSG_TYPE::ADD_FRIEND_REQ)},
                                    {"data",
                                     {{"userid", senderId},
                                      {"username", senderName},
                                      {"friendid", receiverId},
                                      {"friendname", receiverName}}}};
    Call(m_first->Connection(), friendRequest);
    const auto deliveredRequest = m_second->ReadJson();
    EXPECT_EQ(deliveredRequest.at("msgtype").get<int>(),
              static_cast<int>(E_MSG_TYPE::ADD_FRIEND_REQ));
    EXPECT_EQ(deliveredRequest.at("data").at("senderid").get<int64_t>(), senderId);

    // receiver用户接受好友请求后，好友关系写入im_friend表，
    // 同时好友申请从im_friendrequest表中删除；sender用户能接收到receive用户的同意请求
    nlohmann::json friendAck = {{"msgtype", static_cast<int>(E_MSG_TYPE::ADD_FRIEND_ACK)},
                                {"data",
                                 {{"userid", receiverId},
                                  {"username", receiverName},
                                  {"friendid", senderId},
                                  {"friendname", senderName},
                                  {"ackType", FriendState::ACCEPT}}}};
    Call(m_second->Connection(), friendAck);
    const auto deliveredAck = m_first->ReadJson();
    EXPECT_EQ(deliveredAck.at("msgtype").get<int>(), static_cast<int>(E_MSG_TYPE::ADD_FRIEND_ACK));

    MySqlDB friendDb;
    ASSERT_TRUE(friendDb.Connect());
    MYSQL_RES* relation =
        friendDb.Query("select count(*) from im_friend where userid=" + std::to_string(senderId) +
                       " and friendid=" + std::to_string(receiverId));
    ASSERT_NE(relation, nullptr);
    MYSQL_ROW relationRow = mysql_fetch_row(relation);
    ASSERT_NE(relationRow, nullptr);
    EXPECT_EQ(std::string(relationRow[0]), "1");
    mysql_free_result(relation);

    // receiver下线
    m_service->ClientCloseException(m_second->Connection());
    EXPECT_EQ(UserChangeModel().QueryState(receiverId), "offline");

    // sender用户能向receive用户发送消息
    nlohmann::json chat = {
        {"msgtype", static_cast<int>(E_MSG_TYPE::ONE_CHAT_MSG)},
        {"data", {{"receiverId", std::to_string(receiverId)}, {"text", "offline chat payload"}}}};
    Call(m_first->Connection(), chat);
    const auto queued = OfflineMsgModel().Query(receiverId);
    for (auto it = queued.begin(); it != queued.end(); ++it) {
        std::cout << "Offlinemessage: " << *it << std::endl;
    }
    ASSERT_EQ(queued.size(), 1);
    bool foundChat = false;
    for (const auto& message : queued) {
        const auto queuedJson = nlohmann::json::parse(message);
        const int messageType = queuedJson.at("msgtype").get<int>();
        if (messageType == static_cast<int>(E_MSG_TYPE::ONE_CHAT_MSG)) {
            foundChat =
                queuedJson.at("data").at("text").get<std::string>() == "offline chat payload";
        }
    }
    EXPECT_TRUE(foundChat);

    // 已下线的receiver用户向sender用户发送消息
    nlohmann::json forgedChat = {{"msgtype", static_cast<int>(E_MSG_TYPE::ONE_CHAT_MSG)},
                                 {"data",
                                  {{"senderId", receiverId},
                                   {"receiverId", senderId},
                                   {"text", "forged unauthenticated chat"}}}};
    Call(m_second->Connection(), forgedChat);

    EXPECT_FALSE(m_second->HasReadableData(250));
    EXPECT_TRUE(OfflineMsgModel().Query(senderId).empty());
}

TEST_F(OneChatIntegrationTest, UnauthenticatedConnectionCannotSendChat) {
    const auto receiver = Register(*m_second, "13777777777", "service_spoof_chat_receiver",
                                   "onechat-test-password", "test-avatar");
    ASSERT_EQ(receiver.at("data").at("errtype").get<int>(),
              static_cast<int>(E_ERR_TYPE::REG_MSG_ACK_SUCCESS));
    const int64_t receiverId = receiver.at("data").at("userid").get<int64_t>();
    const auto receiverName  = receiver.at("data").at("username").get<std::string>();
    const auto login         = Login(*m_second, receiverName, "onechat-test-password");
    ASSERT_EQ(login.at("data").at("errtype").get<int>(),
              static_cast<int>(E_ERR_TYPE::LOGIN_MSG_ACK_SUCCESS));
    TrackLogin(receiverId, *m_second);

    nlohmann::json forgedChat = {{"msgtype", static_cast<int>(E_MSG_TYPE::ONE_CHAT_MSG)},
                                 {"data",
                                  {{"senderId", "123456789"},
                                   {"receiverId", std::to_string(receiverId)},
                                   {"text", "forged unauthenticated chat"}}}};
    Call(m_first->Connection(), forgedChat);

    EXPECT_FALSE(m_second->HasReadableData(250));
    EXPECT_TRUE(OfflineMsgModel().Query(receiverId).empty());
}

TEST_F(OneChatIntegrationTest, ChatSenderIdComesFromAuthenticatedConnection) {
    const auto sender   = Register(*m_first, "13611111111", "service_chat_sender_identity",
                                   "onechat-test-password", "test-avatar");
    const auto receiver = Register(*m_second, "13611111112", "service_chat_receiver_identity",
                                   "onechat-test-password", "test-avatar");
    ASSERT_EQ(sender.at("data").at("errtype").get<int>(),
              static_cast<int>(E_ERR_TYPE::REG_MSG_ACK_SUCCESS));
    ASSERT_EQ(receiver.at("data").at("errtype").get<int>(),
              static_cast<int>(E_ERR_TYPE::REG_MSG_ACK_SUCCESS));
    const int64_t senderId   = sender.at("data").at("userid").get<int64_t>();
    const int64_t receiverId = receiver.at("data").at("userid").get<int64_t>();
    const auto senderName    = sender.at("data").at("username").get<std::string>();
    const auto receiverName  = receiver.at("data").at("username").get<std::string>();

    const auto senderLogin = Login(*m_first, senderName, "onechat-test-password");
    ASSERT_EQ(senderLogin.at("data").at("errtype").get<int>(),
              static_cast<int>(E_ERR_TYPE::LOGIN_MSG_ACK_SUCCESS));
    TrackLogin(senderId, *m_first);
    const auto receiverLogin = Login(*m_second, receiverName, "onechat-test-password");
    ASSERT_EQ(receiverLogin.at("data").at("errtype").get<int>(),
              static_cast<int>(E_ERR_TYPE::LOGIN_MSG_ACK_SUCCESS));
    TrackLogin(receiverId, *m_second);

    // sender用户向receiver用户发起添加好友申请
    nlohmann::json friendRequest = {{"msgtype", static_cast<int>(E_MSG_TYPE::ADD_FRIEND_REQ)},
                                    {"data",
                                     {{"userid", senderId},
                                      {"username", senderName},
                                      {"friendid", receiverId},
                                      {"friendname", receiverName}}}};
    Call(m_first->Connection(), friendRequest);
    const auto deliveredRequest = m_second->ReadJson();
    EXPECT_EQ(deliveredRequest.at("msgtype").get<int>(),
              static_cast<int>(E_MSG_TYPE::ADD_FRIEND_REQ));
    EXPECT_EQ(deliveredRequest.at("data").at("senderid").get<int64_t>(), senderId);

    // receiver用户接受好友请求后，好友关系写入im_friend表，
    // 同时好友申请从im_friendrequest表中删除；sender用户能接收到receive用户的同意请求
    nlohmann::json friendAck = {{"msgtype", static_cast<int>(E_MSG_TYPE::ADD_FRIEND_ACK)},
                                {"data",
                                 {{"userid", receiverId},
                                  {"username", receiverName},
                                  {"friendid", senderId},
                                  {"friendname", senderName},
                                  {"ackType", FriendState::ACCEPT}}}};
    Call(m_second->Connection(), friendAck);
    const auto deliveredAck = m_first->ReadJson();
    EXPECT_EQ(deliveredAck.at("msgtype").get<int>(), static_cast<int>(E_MSG_TYPE::ADD_FRIEND_ACK));

    MySqlDB friendDb;
    ASSERT_TRUE(friendDb.Connect());
    MYSQL_RES* relation =
        friendDb.Query("select count(*) from im_friend where userid=" + std::to_string(senderId) +
                       " and friendid=" + std::to_string(receiverId));
    ASSERT_NE(relation, nullptr);
    MYSQL_ROW relationRow = mysql_fetch_row(relation);
    ASSERT_NE(relationRow, nullptr);
    EXPECT_EQ(std::string(relationRow[0]), "1");
    mysql_free_result(relation);

    nlohmann::json chat = {{"msgtype", static_cast<int>(E_MSG_TYPE::ONE_CHAT_MSG)},
                           {"data",
                            {{"senderId", "123456789"},
                             {"receiverId", std::to_string(receiverId)},
                             {"text", "authenticated sender"}}}};
    Call(m_first->Connection(), chat);
    const auto delivered = m_second->ReadJson();
    EXPECT_EQ(delivered.at("data").at("senderId").get<std::string>(), std::to_string(senderId));
}

TEST_F(OneChatIntegrationTest, SendMessageByRedis) {
    const auto remote =
        Register(*m_first, "16888888888", "service_remote", "onechat-test-password", "test-avatar");
    ASSERT_EQ(remote.at("data").at("errtype").get<int>(),
              static_cast<int>(E_ERR_TYPE::REG_MSG_ACK_SUCCESS));
    const int64_t remoteId = remote.at("data").at("userid").get<int64_t>();
    ASSERT_TRUE(UserChangeModel().UpdateState(remoteId, "online"));

    // 发送方必须已认证；接收方只标记为在线，不登录到当前 Service。
    // 注册登录发送方
    const auto sender = Register(*m_second, "16888888889", "service_redis_sender",
                                 "onechat-test-password", "test-avatar");
    ASSERT_EQ(sender.at("data").at("errtype").get<int>(),
              static_cast<int>(E_ERR_TYPE::REG_MSG_ACK_SUCCESS));
    const int64_t senderId = sender.at("data").at("userid").get<int64_t>();
    const auto senderName  = sender.at("data").at("username").get<std::string>();
    const auto senderLogin = Login(*m_second, senderName, "onechat-test-password");
    ASSERT_EQ(senderLogin.at("data").at("errtype").get<int>(),
              static_cast<int>(E_ERR_TYPE::LOGIN_MSG_ACK_SUCCESS));
    TrackLogin(senderId, *m_second);

    // 只需建立发送方向的好友关系，聊天服务会据此校验发送权限。
    FriendModel friendModel;
    friendModel.Insert(senderId, remoteId);
    ASSERT_TRUE(friendModel.IsFriend(senderId, remoteId));

    std::mutex remoteMutex;
    std::condition_variable remoteCondition;
    bool remoteReceived = false;
    std::string remoteMessage;
    int64_t remoteChannel = 0;
    Redis remoteRedis;
    ASSERT_TRUE(remoteRedis.Connect());
    remoteRedis.InitNotifyHandler([&](int64_t channel, const std::string& message) {
        {
            std::lock_guard<std::mutex> lock(remoteMutex);
            remoteChannel  = channel;
            remoteMessage  = message;
            remoteReceived = true;
        }
        remoteCondition.notify_one();
    });
    ASSERT_TRUE(remoteRedis.Subscribe(remoteId));

    nlohmann::json remoteChat = {{"msgtype", static_cast<int>(E_MSG_TYPE::ONE_CHAT_MSG)},
                                 {"data", {{"receiverId", remoteId}, {"text", "remote payload"}}}};
    Call(m_second->Connection(), remoteChat);
    {
        std::unique_lock<std::mutex> lock(remoteMutex);
        ASSERT_TRUE(remoteCondition.wait_for(lock, std::chrono::seconds(3),
                                             [&] { return remoteReceived; }));
        EXPECT_EQ(remoteChannel, remoteId);
        const auto forwarded = nlohmann::json::parse(remoteMessage);
        EXPECT_EQ(forwarded.at("msgtype").get<int>(), static_cast<int>(E_MSG_TYPE::ONE_CHAT_MSG));
        EXPECT_EQ(forwarded.at("data").at("senderId").get<std::string>(), std::to_string(senderId));
        EXPECT_EQ(forwarded.at("data").at("receiverId").get<int64_t>(), remoteId);
        EXPECT_EQ(forwarded.at("data").at("text").get<std::string>(), "remote payload");
    }
    EXPECT_TRUE(remoteRedis.Unsubscribe(remoteId));
}
