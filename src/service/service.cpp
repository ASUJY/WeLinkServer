#include "service.h"

#include <muduo/base/Logging.h>

#include <iostream>

#include "common.h"
#include "snowflake.h"

// 单例
Service* Service::instance() {
    static Service service;  // C++11 保证局部静态变量初始化线程安全
    return &service;
}

Service::Service() {
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

    m_msgHandlerMap.insert({E_MSG_TYPE::ADD_FRIEND_ACK_AGREE,
                            std::bind(&Service::AddFriendAckAgree, this, std::placeholders::_1,
                                      std::placeholders::_2, std::placeholders::_3)});

    m_msgHandlerMap.insert({ONE_CHAT_MSG, std::bind(&Service::oneChat, this, std::placeholders::_1,
                                                    std::placeholders::_2, std::placeholders::_3)});
}

MsgHandler Service::GetHandler(int msgType) {
    auto it = m_msgHandlerMap.find(msgType);
    if (it == m_msgHandlerMap.end()) {
        return [=](const muduo::net::TcpConnectionPtr& conn, nlohmann::json& js,
                   muduo::Timestamp time) {
            LOG_ERROR << "msgType:" << msgType << "can not find handler!";
        };
    }
    return it->second;
}

void Service::ClientCloseException(const muduo::net::TcpConnectionPtr& conn) {
    {
        std::lock_guard<std::mutex> locker(m_connMtx);
        for (auto it = m_userConnMap.begin(); it != m_userConnMap.end(); ++it) {
            if (it->second == conn) {
                LOG_ERROR << "连接发送错误，断开";
                m_userConnMap.erase(it);
                break;
            }
        }
    }
}

void Service::Login(const muduo::net::TcpConnectionPtr& conn, nlohmann::json& js,
                    muduo::Timestamp time) {
    std::unordered_map<std::string, std::string> data = js["data"];

    // 这里应该根据客户端传过来的用户名，或者账号，或者手机号来进行登录
    User user;
    user.SetUserName(data["username"]);
    user.SetPwd(data["password"]);

    bool success = m_userModel.Query(user);
    nlohmann::json response;

    if (success) {
        {
            std::lock_guard<std::mutex> locker(m_connMtx);
            m_userConnMap.insert({user.GetUserId(), conn});
        }

        LOG_INFO << "login success!!!!";
        response["msgtype"] = LOGIN_MSG_ACK_SUCCESS;
        LOG_INFO << user.GetUserName() << " ddd " << user.GetUserId();
        response["data"]["userid"]   = user.GetUserId();
        response["data"]["username"] = user.GetUserName();
        response["data"]["phone"]    = user.GetUserPhone();
        response["data"]["avator"]   = user.GetUserAvatar();

        // 查询该用户是否有离线消息
        vector<string> vec = m_offlineMsgModel.Query(user.GetUserId());
        if (!vec.empty()) {
            response["offlinemsg"] = vec;
            m_offlineMsgModel.Remove(user.GetUserId());
        }
    } else {
        LOG_INFO << "login failed!!!!";
        response["msgtype"] = LOGIN_MSG_ACK_FAIL;
    }
    conn->send(response.dump());
}

void Service::Register(const muduo::net::TcpConnectionPtr& conn, nlohmann::json& js,
                       muduo::Timestamp time) {
    std::map<std::string, std::string> data = js["data"];

    User user;
    user.SetUserPhone(data["phone"]);
    user.SetUserName(data["username"]);
    user.SetPwd(data["password"]);
    user.SetUserAvatar(data["avatar"]);

    uint32_t workerId     = 1;
    uint32_t datacenterId = 1;
    SnowflakeGenerator gen(workerId, datacenterId);
    user.SetUserId(gen.NextId());

    bool state = m_userModel.Insert(user);
    nlohmann::json response;
    if (state) {
        // register success
        LOG_INFO << "register success!!!!";
        response["msgtype"]          = REG_MSG_ACK_SUCCESS;
        response["data"]["userid"]   = user.GetUserId();
        response["data"]["username"] = user.GetUserName();
        response["data"]["phone"]    = user.GetUserPhone();
        response["data"]["avator"]   = user.GetUserAvatar();
    } else {
        // register failed
        response["msgtype"] = REG_MSG_ACK_FAIL;
    }
    conn->send(response.dump());
}

void Service::GetFriendInfo(const muduo::net::TcpConnectionPtr& conn, nlohmann::json& js,
                            muduo::Timestamp time) {
    std::map<std::string, std::string> data = js["data"];
    nlohmann::json response;
    std::vector<std::string> resVec = m_userModel.QueryUserInfo(data["username"]);
    if (!resVec.empty()) {
        response["msgtype"]          = GET_FRIEND_INFO_SUCCESS;
        response["data"]["userid"]   = resVec[0];
        response["data"]["username"] = resVec[2];
        response["data"]["phone"]    = resVec[1];
        // conn->send(response.dump());
    } else {
        response["msgtype"] = GET_FRIEND_INFO_FAILED;
        LOG_INFO << "获取朋友失败";
    }
    conn->send(response.dump());
}

void Service::AddFriendReq(const muduo::net::TcpConnectionPtr& conn, nlohmann::json& js,
                           muduo::Timestamp time) {
    std::map<std::string, std::string> data = js["data"];
    nlohmann::json response;
    int userid             = stoi(data["userid"]);
    std::string username   = data["username"];
    int friendid           = stoi(data["friendid"]);
    std::string friendname = data["friendname"];

    response["data"]["senderid"]     = userid;
    response["data"]["sendername"]   = username;
    response["data"]["receiverid"]   = friendid;
    response["data"]["receivername"] = friendname;
    response["msgtype"]              = ADD_FRIEND_REQ;

    {
        std::lock_guard<std::mutex> locker(m_connMtx);
        if (m_userConnMap.find(friendid) != m_userConnMap.end()) {
            m_userConnMap[friendid]->send(response.dump());
        } else {
            // 用户离线
        }
    }
}

void Service::AddFriendAckAgree(const muduo::net::TcpConnectionPtr& conn, nlohmann::json& js,
                                muduo::Timestamp time) {
    std::map<std::string, std::string> data = js["data"];
    nlohmann::json response;
    int userid             = stoi(data["userid"]);
    std::string username   = data["username"];
    int friendid           = stoi(data["friendid"]);
    std::string friendname = data["friendname"];

    response["data"]["senderid"]     = userid;
    response["data"]["sendername"]   = username;
    response["data"]["receiverid"]   = friendid;
    response["data"]["receivername"] = friendname;
    response["msgtype"]              = ADD_FRIEND_ACK_AGREE;
    LOG_INFO << "Service::AddFriendAckAgree: userid" << userid << " username: " << username
             << " friendid: " << friendid << " friendname: " << friendname;

    {
        std::lock_guard<std::mutex> locker(m_connMtx);
        if (m_userConnMap.find(friendid) != m_userConnMap.end()) {
            m_userConnMap[friendid]->send(response.dump());
            m_friendModel.Insert(userid, friendid);
            m_friendModel.Insert(friendid, userid);
        } else {
            // 用户离线
        }
    }
}

void Service::oneChat(const muduo::net::TcpConnectionPtr& conn, nlohmann::json& js,
                      muduo::Timestamp time) {
    std::map<std::string, std::string> data = js["data"];

    uint64_t receiverid = stol(data["receiverId"]);
    LOG_INFO << "Service::oneChat userid: " << receiverid;
    {
        std::lock_guard<std::mutex> locker(m_connMtx);
        auto it = m_userConnMap.find(receiverid);
        if (it != m_userConnMap.end()) {
            it->second->send(js.dump());
            return;
        }
    }
    m_offlineMsgModel.Insert(receiverid, js.dump());
}