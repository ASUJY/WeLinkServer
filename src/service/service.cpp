#include "service.h"

#include <muduo/base/Logging.h>

#include <iostream>
#include <string>

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
    uint64_t id = 0;
    {
        std::lock_guard<std::mutex> locker(m_connMtx);
        for (auto it = m_userConnMap.begin(); it != m_userConnMap.end(); ++it) {
            if (it->second == conn) {
                LOG_ERROR << "连接发送错误，断开";
                id = it->first;
                m_userConnMap.erase(it);
                break;
            }
        }
    }
    // 用户注销，相当于就是下线，在redis中取消订阅通道
    m_redis.Unsubscribe(id);

    // 更新用户的状态信息
    if (id != -1) {
        m_userChangeModel.UpdateState(id, "offline");
    }
}

void Service::Login(const muduo::net::TcpConnectionPtr& conn, nlohmann::json& js,
                    muduo::Timestamp time) {
    std::unordered_map<std::string, std::string> data = js["data"];
    std::cout << "Login: " << std::endl;
    // 这里应该根据客户端传过来的用户名，或者账号，或者手机号来进行登录
    User user;
    user.SetUserName(data["username"]);
    user.SetPwd(data["password"]);

    bool res1 =
        m_userModel.QueryAccount(user, static_cast<E_ACCOUNT_TYPE>(stoi(data["accountType"])));

    nlohmann::json response;
    response["msgtype"] = E_MSG_TYPE::LOGIN_MSG_ACK;
    if (res1) {
        if (user.GetPwd() == data["password"]) {
            auto state = m_userChangeModel.QueryState(user.GetUserId());
            if (state.empty()) {
                response["data"]["errtype"] = E_ERR_TYPE::USER_NO_REGISTER;
            } else if (state == "online") {
                // 该用户已经登录，不允许重复登录
                response["data"]["errtype"] = E_ERR_TYPE::USER_ONLINE;
                // response["errno"]   = 2;
                // response["errmsg"]  = "该账号已经登录，请重新输入新账号";
            } else {
                // 登录成功，记录用户连接信息
                {
                    std::lock_guard<std::mutex> locker(m_connMtx);
                    m_userConnMap.insert({user.GetUserId(), conn});
                }

                // id用户登录成功后，向redis订阅channel(id)
                m_redis.Subscribe(user.GetUserId());

                // 登录成功，更新用户状态信息 state offline=>online
                user.SetUserState("online");
                m_userChangeModel.UpdateState(user);

                LOG_INFO << "login success!!!!";
                response["data"]["errtype"] = E_ERR_TYPE::LOGIN_MSG_ACK_SUCCESS;
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

                // 查询该用户的好友信息并返回，这里可以优化一下，看看是否能获取用户登录设备的信息
                // 如果是常用登录设备就不需要返回好友信息了
            }
        } else {
            LOG_INFO << "login failed!!!!";
            response["data"]["errtype"] = E_ERR_TYPE::ACCOUNT_ERROR;
        }
    } else {
        LOG_INFO << "login failed!!!!";

        response["data"]["errtype"] = E_ERR_TYPE::USER_NO_REGISTER;
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
    m_userChangeModel.Insert(user);
    nlohmann::json response;
    if (state) {
        // register success
        LOG_INFO << "register success!!!!";
        response["msgtype"]          = E_MSG_TYPE::REG_MSG_ACK;
        response["data"]["errtype"]  = E_ERR_TYPE::REG_MSG_ACK_SUCCESS;
        response["data"]["userid"]   = user.GetUserId();
        response["data"]["username"] = user.GetUserName();
        response["data"]["phone"]    = user.GetUserPhone();
        response["data"]["avator"]   = user.GetUserAvatar();
    } else {
        // register failed
        response["msgtype"]         = E_MSG_TYPE::REG_MSG_ACK;
        response["data"]["errtype"] = E_ERR_TYPE::USER_EXIT;
    }
    conn->send(response.dump());
}

void Service::GetFriendInfo(const muduo::net::TcpConnectionPtr& conn, nlohmann::json& js,
                            muduo::Timestamp time) {
    std::map<std::string, std::string> data = js["data"];
    nlohmann::json response;
    std::vector<std::string> resVec = m_userModel.QueryUserInfo(data["username"]);
    if (!resVec.empty()) {
        response["msgtype"]          = E_MSG_TYPE::GET_FRIEND_INFO_SUCCESS;
        response["data"]["userid"]   = resVec[0];
        response["data"]["username"] = resVec[2];
        response["data"]["phone"]    = resVec[1];
        // conn->send(response.dump());
    } else {
        response["msgtype"] = E_MSG_TYPE::GET_FRIEND_INFO_FAILED;
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
    response["msgtype"]              = E_MSG_TYPE::ADD_FRIEND_REQ;

    {
        std::lock_guard<std::mutex> locker(m_connMtx);
        if (m_userConnMap.find(friendid) != m_userConnMap.end()) {
            m_userConnMap[friendid]->send(response.dump());
        } else {
            // 查询用户是否在线
            auto state = m_userChangeModel.QueryState(friendid);
            if (state == "online") {
                m_redis.Publish(friendid, response.dump());
                return;
            }

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
    response["msgtype"]              = E_MSG_TYPE::ADD_FRIEND_ACK_AGREE;
    LOG_INFO << "Service::AddFriendAckAgree: userid" << userid << " username: " << username
             << " friendid: " << friendid << " friendname: " << friendname;

    {
        std::lock_guard<std::mutex> locker(m_connMtx);
        if (m_userConnMap.find(friendid) != m_userConnMap.end()) {
            m_userConnMap[friendid]->send(response.dump());
            m_friendModel.Insert(userid, friendid);
            m_friendModel.Insert(friendid, userid);
        } else {
            // 查询用户是否在线
            auto state = m_userChangeModel.QueryState(friendid);
            if (state == "online") {
                m_friendModel.Insert(userid, friendid);
                m_friendModel.Insert(friendid, userid);
                m_redis.Publish(friendid, response.dump());
                return;
            }

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

    // 查询toid是否在线
    auto state = m_userChangeModel.QueryState(receiverid);
    if (state == "online") {
        m_redis.Publish(receiverid, js.dump());
        return;
    }

    m_offlineMsgModel.Insert(receiverid, js.dump());
}

void Service::HandleRedisSubscribeMessage(uint64_t userid, const std::string& msg) {
    {
        std::lock_guard<mutex> lock(m_connMtx);
        auto it = m_userConnMap.find(userid);
        if (it != m_userConnMap.end()) {
            it->second->send(msg);
            return;
        }
    }
    m_offlineMsgModel.Insert(userid, msg);
}