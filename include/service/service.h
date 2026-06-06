#ifndef SERVICE_H
#define SERVICE_H

#include <muduo/net/TcpConnection.h>
#include <unordered_map>

#include "json.hpp"
#include "usermodel.h"

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
    Service();  // 构造函数私有化，禁止外部创建

private:
    std::unordered_map<int, MsgHandler> m_msgHandlerMap;

    // 数据操作类对象
    UserModel m_userModel;
};

#endif