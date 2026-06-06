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