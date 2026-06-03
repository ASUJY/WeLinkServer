#include "service.h"
#include "common.h"
#include <muduo/base/Logging.h>

// 单例
Service* Service::instance() {
    static Service service;
    return &service;
}

Service::Service() {
    m_msgHandlerMap.insert(
        {E_MSG_TYPE::LOGIN_MSG, std::bind(&Service::Login, 
        this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3)});
    
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
    } else {
        return m_msgHandlerMap[msgType];
    }
}

void Service::ClientCloseException(const muduo::net::TcpConnectionPtr &conn) {

}

void Service::Login(const muduo::net::TcpConnectionPtr &conn, 
    nlohmann::json &js, muduo::Timestamp time) {
    
}

void Service::Register(const muduo::net::TcpConnectionPtr &conn, 
    nlohmann::json &js, muduo::Timestamp time) {
    
}