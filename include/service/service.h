#ifndef SERVICE_H
#define SERVICE_H

#include <muduo/net/TcpConnection.h>
#include <unordered_map>

#include "json.hpp"

using MsgHandler = std::function<
    void(const muduo::net::TcpConnectionPtr &conn, 
            nlohmann::json &js, muduo::Timestamp time)>;

class Service {
public:
    static Service* instance();
    MsgHandler GetHandler(int msgType);
    void ClientCloseException(const muduo::net::TcpConnectionPtr &conn);

    void Login(const  muduo::net::TcpConnectionPtr &conn, 
                nlohmann::json &js, muduo::Timestamp time);
    
    void Register(const  muduo::net::TcpConnectionPtr &conn, 
                nlohmann::json &js, muduo::Timestamp time);

private:
    Service();

private:
    std::unordered_map<int, MsgHandler> m_msgHandlerMap;
};

#endif