#include "service.h"
#include "common.h"
#include <muduo/base/Logging.h>
#include <iostream>

// 单例
Service* Service::instance() {
    static Service service; // C++11 保证局部静态变量初始化线程安全
    return &service;
}

Service::Service() {
    // m_msgHandlerMap.insert(
    //     {E_MSG_TYPE::LOGIN_MSG, std::bind(&Service::Login, 
    //     this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)});

    // 等同于与上面的写法
    m_msgHandlerMap.insert({E_MSG_TYPE::LOGIN_MSG, 
        [this](auto&& conn, auto&& js, auto&& time) {
            this->Login(conn, js, time);
        }});

    m_msgHandlerMap.insert(
        {E_MSG_TYPE::REG_MSG, std::bind(&Service::Register, 
        this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3
        )}
    );

    m_msgHandlerMap.insert(
        {E_MSG_TYPE::GET_FRIEND_INFO_REQ, std::bind(&Service::GetFriendInfo, 
        this,  std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)});

    m_msgHandlerMap.insert(
    {E_MSG_TYPE::ADD_FRIEND_REQ, std::bind(&Service::AddFriendReq,
    this,  std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)});
}

MsgHandler Service::GetHandler(int msgType) {
    auto it = m_msgHandlerMap.find(msgType);
    if (it == m_msgHandlerMap.end()) {
        return [=](const muduo::net::TcpConnectionPtr &conn, 
            nlohmann::json &js, muduo::Timestamp time){
                LOG_ERROR << "msgType:" << msgType << "can not find handler!";
            };
    } 
    return it->second;
    
}

void Service::ClientCloseException(const muduo::net::TcpConnectionPtr &conn) {

}

void Service::Login(const muduo::net::TcpConnectionPtr &conn, 
    nlohmann::json &js, muduo::Timestamp time) {
    std::unordered_map<std::string, std::string> data = js["data"];

    // 这里应该根据客户端传过来的用户名，或者账号，或者手机号来进行登录
    User user;
    user.SetUserName(data["username"]);
    user.SetPwd(data["password"]);

    bool success = m_userModel.Query(user);
    nlohmann::json response;

    if (success) {
        LOG_INFO << "login success!!!!";
        response["msgtype"] = LOGIN_MSG_ACK_SUCCESS;
        LOG_INFO << user.GetUserName() << " ddd " << user.GetUserId();
        response["data"]["username"] = user.GetUserName();
        response["data"]["userid"] = user.GetUserId();
        {
            std::lock_guard<std::mutex> locker(m_connMtx);
            m_userConnMap.insert({user.GetUserId(), conn});
        }
    } else {
        response["msgtype"] = LOGIN_MSG_ACK_FAIL;
    }
    conn->send(response.dump());
}

void Service::Register(const muduo::net::TcpConnectionPtr &conn, 
    nlohmann::json &js, muduo::Timestamp time) {
    std::map<std::string, std::string> data = js["data"];

    User user;
    user.SetUserPhone(data["phone"]);
    user.SetUserName(data["username"]);
    user.SetPwd(data["password"]);

    bool state = m_userModel.Insert(user);
    nlohmann::json response;
    if (state) {
        // register success
        LOG_INFO << "register success!!!!";
        response["msgtype"] = REG_MSG_ACK_SUCCESS;
    } else {
        // register failed
        response["msgtype"] = REG_MSG_ACK_SUCCESS;
    }
    conn->send(response.dump());
}

void Service::GetFriendInfo(const muduo::net::TcpConnectionPtr &conn, 
    nlohmann::json &js, muduo::Timestamp time) {
    std::map<std::string, std::string> data = js["data"];
    std::cout << data["username"] << std::endl;
    nlohmann::json response;
    std::vector<std::string> resVec = m_userModel.QueryUserInfo(data["username"]);
    if (!resVec.empty()) {
        response["msgtype"] = GET_FRIEND_INFO_SUCCESS;
        response["data"]["userid"] = resVec[0];
        response["data"]["username"] = resVec[1];
        response["data"]["phone"] = resVec[2];
        LOG_INFO << resVec[0] << " GetFriendInfo " << resVec[1];
        conn->send(response.dump());
    }
}

void Service::AddFriendReq(const muduo::net::TcpConnectionPtr &conn, 
    nlohmann::json &js, muduo::Timestamp time) {
    std::map<std::string, std::string> data = js["data"];
    nlohmann::json response;
     std::cout << "00000" << std::endl;
    int userid = stoi(data["userid"]);
     std::cout << "1111" << std::endl;
    std::string username = data["username"];
     std::cout << "2222" << std::endl;
    int friendid = stoi(data["friendid"]);
     std::cout << "3333" << std::endl;
    std::string friendname = data["friendname"];
    std::cout << "hhhhh" << std::endl;
    if (m_userConnMap.find(friendid) != m_userConnMap.end()) {
        nlohmann::json response;
        response["senderid"] = userid;
        response["sendername"] = username;
        response["receiverid"] = friendid;
        response["receivername"] = friendname;
        response["msgtype"] = ADD_FRIEND_REQ;
        m_userConnMap[friendid]->send(response.dump());
    } else {
        // 用户离线
    }

    
}