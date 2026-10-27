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
    : m_registrationService(m_userModel, m_userChangeModel),
      m_threadPool(new ThreadPool(4, 1000)),
      m_loginService(m_userModel, m_userChangeModel, m_offlineMsgModel) {
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
        {E_MSG_TYPE::ONE_CHAT_MSG, std::bind(&Service::oneChat, this, std::placeholders::_1,
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
    std::map<std::string, std::string> data = js["data"];
    nlohmann::json response;
    response["msgtype"]             = E_MSG_TYPE::GET_FRIEND_INFO_ACK;
    std::vector<std::string> resVec = m_userModel.QueryUserInfo(
        data["username"], static_cast<E_ACCOUNT_TYPE>(stoi(data["accountType"])));
    if (!resVec.empty()) {
        response["data"]["userid"]   = resVec[0];
        response["data"]["username"] = resVec[2];
        response["data"]["phone"]    = resVec[1];
        response["data"]["errtype"]  = E_ERR_TYPE::GET_FRIEND_INFO_SUCCESS;
    } else {
        response["data"]["errtype"] = E_ERR_TYPE::GET_FRIEND_INFO_FAILED;
        LOG_INFO << "获取朋友失败";
    }
    SendJson(conn, response);
}

void Service::AddFriendReq(const muduo::net::TcpConnectionPtr& conn, nlohmann::json& js,
                           muduo::Timestamp time) {
    std::map<std::string, std::string> data = js["data"];
    nlohmann::json response;
    int64_t userid = -1;
    if (!GetAuthenticatedUserId(conn, userid)) {
        LOG_WARN << "Reject friend request from unauthenticated connection";
        return;
    }
    std::string username   = data["username"];
    int64_t friendid       = stoll(data["friendid"]);
    std::string friendname = data["friendname"];

    response["data"]["senderid"]     = userid;
    response["data"]["sendername"]   = username;
    response["data"]["receiverid"]   = friendid;
    response["data"]["receivername"] = friendname;
    response["msgtype"]              = E_MSG_TYPE::ADD_FRIEND_REQ;

    {
        std::lock_guard<std::mutex> locker(m_connMtx);
        if (m_userConnMap.find(friendid) != m_userConnMap.end()) {
            // m_userConnMap[friendid]->send(response.dump());
            SendJson(m_userConnMap[friendid], response);
        } else {
            // 查询用户是否在线
            auto state = m_userChangeModel.QueryState(friendid);
            if (state == "online") {
                m_redis.Publish(friendid, response.dump());
                return;
            }

            // 用户离线
            m_offlineMsgModel.Insert(friendid, response.dump());
        }
    }
}

void Service::AddFriendAck(const muduo::net::TcpConnectionPtr& conn, nlohmann::json& js,
                           muduo::Timestamp time) {
    std::map<std::string, std::string> data = js["data"];
    nlohmann::json response;
    int64_t userid = -1;
    if (!GetAuthenticatedUserId(conn, userid)) {
        LOG_WARN << "Reject friend reply from unauthenticated connection";
        return;
    }
    std::string username   = data["username"];
    int64_t friendid       = stoll(data["friendid"]);
    std::string friendname = data["friendname"];

    response["data"]["senderid"]     = userid;
    response["data"]["sendername"]   = username;
    response["data"]["receiverid"]   = friendid;
    response["data"]["receivername"] = friendname;
    response["data"]["ackType"]      = data["ackType"];
    response["msgtype"]              = E_MSG_TYPE::ADD_FRIEND_ACK;
    LOG_INFO << "Service::AddFriendAckAgree: userid" << userid << " username: " << username
             << " friendid: " << friendid << " friendname: " << friendname
             << "status: " << data["ackType"];

    {
        std::lock_guard<std::mutex> locker(m_connMtx);
        if (m_userConnMap.find(friendid) != m_userConnMap.end()) {
            if (static_cast<FriendState>(stoi(data["ackType"])) == FriendState::ACCEPT) {
                m_friendModel.Insert(userid, friendid);
                m_friendModel.Insert(friendid, userid);
            }
            // m_userConnMap[friendid]->send(response.dump());
            SendJson(m_userConnMap[friendid], response);
        } else {
            // 查询用户是否在线
            auto state = m_userChangeModel.QueryState(friendid);
            if (state == "online") {
                if (static_cast<FriendState>(stoi(data["ackType"])) == FriendState::ACCEPT) {
                    m_friendModel.Insert(userid, friendid);
                    m_friendModel.Insert(friendid, userid);
                }
                m_redis.Publish(friendid, response.dump());
                return;
            }

            // 用户离线
            m_offlineMsgModel.Insert(friendid, response.dump());
        }
    }
}

void Service::oneChat(const muduo::net::TcpConnectionPtr& conn, nlohmann::json& js,
                      muduo::Timestamp time) {
    std::map<std::string, std::string> data = js["data"];

    int64_t senderId = -1;
    if (!GetAuthenticatedUserId(conn, senderId)) {
        LOG_WARN << "Reject chat from unauthenticated connection";
        return;
    }

    int64_t receiverid = stoll(data["receiverId"]);
    LOG_INFO << "Service::oneChat userid: " << receiverid;

    // 客户端提供的发送方身份不可信，转发前用当前连接的登录身份覆盖。
    js["data"]["senderId"] = std::to_string(senderId);
    {
        std::lock_guard<std::mutex> locker(m_connMtx);
        auto it = m_userConnMap.find(receiverid);
        if (it != m_userConnMap.end()) {
            // it->second->send(js.dump());
            LOG_INFO << "Service::oneChat js: " << js.dump();
            SendJson(it->second, js);
            return;
        }
    }

    // 查询toid是否在线
    auto state = m_userChangeModel.QueryState(receiverid);
    if (state == "online") {
        LOG_INFO << "redis Service::oneChat js: " << js.dump();
        m_redis.Publish(receiverid, js.dump());
        return;
    }

    // 用户不在线则暂时到服务器数据库中的离线消息表
    m_offlineMsgModel.Insert(receiverid, js.dump());
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