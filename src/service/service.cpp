#include "service.h"

#include <muduo/base/Logging.h>
#include <muduo/net/EventLoop.h>

#include <iostream>
#include <string>

#include "common.h"
#include "protocol.h"
#include "snowflake.h"

// 单例
Service* Service::instance() {
    static Service service;  // C++11 保证局部静态变量初始化线程安全
    return &service;
}

Service::Service()
    : m_threadPool(new ThreadPool(4, 1000)),
      m_registrationService(m_userModel, m_userChangeModel),
      m_loginService(m_userModel, m_userChangeModel, m_offlineMsgModel),
      m_friendService(m_userModel, m_friendModel,
                      [this](int64_t userId, const nlohmann::json& message) {
                          DeliverToUser(userId, message);
                      }),
      m_chatService([this](int64_t userId, const nlohmann::json& message) {
          DeliverToUser(userId, message);
      }) {
    // m_msgHandlerMap.insert(
    //     {E_MSG_TYPE::LOGIN_MSG, std::bind(&Service::Login,
    //     this, std::placeholders::_1, std::placeholders::_2,
    //     std::placeholders::_3)});

    // 等同于与上面的写法
    m_msgHandlerMap.insert({E_MSG_TYPE::LOGIN_MSG, [this](auto&& conn, auto&& js, auto&& time) {
                                this->Login(conn, js, time);
                            }});

    m_msgHandlerMap.insert(
        {E_MSG_TYPE::REG_MSG, std::bind(&Service::Register, this, std::placeholders::_1,
                                        std::placeholders::_2, std::placeholders::_3)});

    m_msgHandlerMap.insert({E_MSG_TYPE::GET_FRIEND_INFO_REQ,
                            std::bind(&Service::GetFriendInfo, this, std::placeholders::_1,
                                      std::placeholders::_2, std::placeholders::_3)});

    m_msgHandlerMap.insert(
        {E_MSG_TYPE::ADD_FRIEND_REQ, std::bind(&Service::AddFriendReq, this, std::placeholders::_1,
                                               std::placeholders::_2, std::placeholders::_3)});

    m_msgHandlerMap.insert(
        {E_MSG_TYPE::ADD_FRIEND_ACK, std::bind(&Service::AddFriendAck, this, std::placeholders::_1,
                                               std::placeholders::_2, std::placeholders::_3)});

    m_msgHandlerMap.insert(
        {E_MSG_TYPE::ONE_CHAT_MSG, std::bind(&Service::OneChat, this, std::placeholders::_1,
                                             std::placeholders::_2, std::placeholders::_3)});

    if (m_redis.Connect()) {
        // 设置redis的回调函数(redis队列中如果有被订阅的channel的信息，就会调用这个回调函数)
        m_redis.InitNotifyHandler(std::bind(&Service::HandleRedisSubscribeMessage, this,
                                            std::placeholders::_1, std::placeholders::_2));
    }
}

MsgHandler Service::GetHandler(int msgType) {
    auto it = m_msgHandlerMap.find(static_cast<E_MSG_TYPE>(msgType));
    if (it == m_msgHandlerMap.end()) {
        return [=](const muduo::net::TcpConnectionPtr& conn, nlohmann::json& js,
                   muduo::Timestamp time) {
            LOG_ERROR << "msgType:" << msgType << "can not find handler!";
        };
    }
    return it->second;
}

void Service::ClientCloseException(const muduo::net::TcpConnectionPtr& conn) {
    if (!conn) {
        return;
    }

    int64_t userId = -1;
    {
        std::lock_guard<std::mutex> locker(m_connMtx);
        const auto userIt = m_connUserMap.find(conn.get());
        if (userIt != m_connUserMap.end()) {
            userId = userIt->second;
        }
    }

    if (userId == -1) {
        return;
    }

    const auto sessionMutex = GetUserSessionMutex(userId);
    std::lock_guard<std::mutex> sessionLocker(*sessionMutex);

    bool wasCurrentConnection = false;
    {
        std::lock_guard<std::mutex> locker(m_connMtx);
        const auto userIt = m_connUserMap.find(conn.get());
        if (userIt != m_connUserMap.end() && userIt->second == userId) {
            m_connUserMap.erase(userIt);
        }

        const auto connIt = m_userConnMap.find(userId);
        if (connIt != m_userConnMap.end() && connIt->second == conn) {
            LOG_INFO << "客户端连接断开";
            m_userConnMap.erase(connIt);
            wasCurrentConnection = true;
        }
    }

    if (wasCurrentConnection) {
        m_redis.Unsubscribe(userId);                       // 用户下线，在redis中取消订阅通道
        m_userChangeModel.UpdateState(userId, "offline");  // 更新用户的状态信息
    }
}

bool Service::GetAuthenticatedUserId(const muduo::net::TcpConnectionPtr& conn, int64_t& userId) {
    if (!conn) {
        return false;
    }
    std::lock_guard<std::mutex> locker(m_connMtx);
    const auto it = m_connUserMap.find(conn.get());
    if (it == m_connUserMap.end()) {
        return false;
    }
    userId = it->second;
    return true;
}

std::shared_ptr<std::mutex> Service::GetUserSessionMutex(int64_t userId) {
    std::lock_guard<std::mutex> locker(m_connMtx);
    auto& sessionMutex = m_userSessionMutexes[userId];
    if (!sessionMutex) {
        sessionMutex = std::make_shared<std::mutex>();
    }
    return sessionMutex;
}

void Service::Login(const muduo::net::TcpConnectionPtr& conn, nlohmann::json& js,
                    muduo::Timestamp time) {
    if (!conn) {
        LOG_WARN << "Reject login request without a connection";
        return;
    }

    nlohmann::json loginInfo = js;
    bool accepted            = false;
    m_threadPool->AppendQueue(accepted, [this, conn, loginInfo = std::move(loginInfo)]() {
        nlohmann::json response;

        try {
            response = m_loginService.Login(loginInfo, [this, &conn](User& user) {
                const int64_t userId    = user.GetUserId();
                const auto sessionMutex = GetUserSessionMutex(userId);
                std::lock_guard<std::mutex> sessionLocker(*sessionMutex);

                {
                    std::lock_guard<std::mutex> locker(m_connMtx);
                    if (!conn->connected()) {
                        return false;
                    }

                    const auto existing = m_userConnMap.find(userId);
                    if (existing != m_userConnMap.end() && existing->second != conn &&
                        existing->second->connected()) {
                        return false;
                    }

                    m_userConnMap[userId]     = conn;
                    m_connUserMap[conn.get()] = userId;
                }
                // 登录成功，更新用户状态信息 state offline=>online
                // 向redis订阅channel(id)
                user.SetUserState("online");
                m_userChangeModel.UpdateState(user);
                m_redis.Subscribe(userId);
                return true;
            });
        } catch (const std::exception& e) {
            LOG_ERROR << "Login processing failed: " << e.what();
            response["msgtype"]         = E_MSG_TYPE::LOGIN_MSG_ACK;
            response["data"]["errtype"] = E_ERR_TYPE::ACCOUNT_ERROR;
        } catch (...) {
            LOG_ERROR << "Login processing failed with an unknown exception";
            response["msgtype"]         = E_MSG_TYPE::LOGIN_MSG_ACK;
            response["data"]["errtype"] = E_ERR_TYPE::ACCOUNT_ERROR;
        }

        conn->getLoop()->queueInLoop(
            [this, conn, response = std::move(response)]() { SendJson(conn, response); });
    });

    if (!accepted) {
        nlohmann::json response;
        response["msgtype"]         = E_MSG_TYPE::LOGIN_MSG_ACK;
        response["data"]["errtype"] = E_ERR_TYPE::SERVER_BUSY;
        SendJson(conn, response);
    }
}

void Service::Register(const muduo::net::TcpConnectionPtr& conn, nlohmann::json& js,
                       muduo::Timestamp time) {
    if (!conn) {
        LOG_WARN << "Reject registration request without a connection";
        return;
    }

    nlohmann::json registerInfo = js;
    bool accepted               = false;
    m_threadPool->AppendQueue(accepted, [this, conn, registerInfo = std::move(registerInfo)]() {
        nlohmann::json response = m_registrationService.Register(registerInfo);
        conn->getLoop()->queueInLoop(
            [this, conn, response = std::move(response)]() { SendJson(conn, response); });
    });

    if (!accepted) {
        nlohmann::json response;
        response["msgtype"]         = E_MSG_TYPE::REG_MSG_ACK;
        response["data"]["errtype"] = E_ERR_TYPE::SERVER_BUSY;
        SendJson(conn, response);
    }
}

void Service::GetFriendInfo(const muduo::net::TcpConnectionPtr& conn, nlohmann::json& js,
                            muduo::Timestamp time) {
    if (!conn) {
        return;
    }

    nlohmann::json request = js;
    bool accepted          = false;
    m_threadPool->AppendQueue(accepted, [this, conn, request]() {
        nlohmann::json response = m_friendService.GetFriendInfo(request);
        conn->getLoop()->queueInLoop(
            [this, conn, response = std::move(response)]() { SendJson(conn, response); });
    });

    if (!accepted) {
        nlohmann::json response;
        response["msgtype"]         = E_MSG_TYPE::GET_FRIEND_INFO_ACK;
        response["data"]["errtype"] = E_ERR_TYPE::SERVER_BUSY;
        SendJson(conn, response);
    }
}

void Service::AddFriendReq(const muduo::net::TcpConnectionPtr& conn, nlohmann::json& js,
                           muduo::Timestamp time) {
    int64_t userid = -1;
    if (!GetAuthenticatedUserId(conn, userid)) {
        LOG_WARN << "Reject friend request from unauthenticated connection";
        return;
    }

    const nlohmann::json request = js;
    bool accepted                = false;
    m_threadPool->AppendQueue(
        accepted, [this, userid, request]() { m_friendService.AddFriendReq(userid, request); });
    if (!accepted) {
        // 应该返回给发送方，告诉他好友请求转发失败
        LOG_WARN << "Friend request rejected: thread pool is busy";
    }
}

void Service::AddFriendAck(const muduo::net::TcpConnectionPtr& conn, nlohmann::json& js,
                           muduo::Timestamp time) {
    int64_t userid = -1;
    if (!GetAuthenticatedUserId(conn, userid)) {
        LOG_WARN << "Reject friend reply from unauthenticated connection";
        return;
    }

    const nlohmann::json request = js;
    bool accepted                = false;
    m_threadPool->AppendQueue(
        accepted, [this, userid, request]() { m_friendService.AddFriendAck(userid, request); });
    if (!accepted) {
        // 应该返回给发送方，告诉他好友请求转发失败
        LOG_WARN << "Friend reply rejected: thread pool is busy";
    }
}

void Service::OneChat(const muduo::net::TcpConnectionPtr& conn, nlohmann::json& js,
                      muduo::Timestamp time) {
    int64_t senderId = -1;
    if (!GetAuthenticatedUserId(conn, senderId)) {
        LOG_WARN << "Reject chat from unauthenticated connection";
        return;
    }

    m_chatService.SendOneChat(senderId, js);
}

void Service::HandleRedisSubscribeMessage(int64_t userid, const std::string& msg) {
    {
        std::lock_guard<mutex> lock(m_connMtx);
        auto it = m_userConnMap.find(userid);
        if (it != m_userConnMap.end()) {
            // it->second->send(msg);
            nlohmann::json js = nlohmann::json::parse(msg);
            SendJson(it->second, js);
            return;
        }
    }
    m_offlineMsgModel.Insert(userid, msg);
}

bool Service::SendJson(const muduo::net::TcpConnectionPtr& conn, const nlohmann::json& response) {
    if (!conn || !conn->connected()) {
        LOG_WARN << "Cannot send JSON: connection is not connected";
        return false;
    }

    std::string body;
    try {
        body = response.dump();
    } catch (const nlohmann::json::exception& e) {
        LOG_ERROR << "JSON serialization failed: " << e.what();
        return false;
    }

    std::string packet;
    if (!protocol::EncodeFrame(body, packet)) {
        LOG_ERROR << "Invalid outgoing message length: " << body.size();
        return false;
    }

    conn->send(packet);

    return true;
}

void Service::DeliverToUser(int64_t userId, const nlohmann::json& message) {
    muduo::net::TcpConnectionPtr conn;

    {
        std::lock_guard<std::mutex> locker(m_connMtx);
        const auto it = m_userConnMap.find(userId);
        if (it != m_userConnMap.end()) {
            conn = it->second;
        }
    }

    if (conn) {
        conn->getLoop()->queueInLoop([this, conn, message]() { SendJson(conn, message); });
        return;
    }

    // 用户是否在其他服务器
    if (m_userChangeModel.QueryState(userId) == "online") {
        m_redis.Publish(userId, message.dump());
        return;
    }

    // 用户离线
    m_offlineMsgModel.Insert(userId, message.dump());
}