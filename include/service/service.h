#ifndef SERVICE_H
#define SERVICE_H

#include <muduo/net/TcpConnection.h>

#include <mutex>
#include <unordered_map>

#include "common.h"
#include "friendmodel.h"
#include "json.hpp"
#include "offlinemessagemodel.h"
#include "redis.h"
#include "registrationservice.h"
#include "userchangemodel.h"
#include "usermodel.h"

using MsgHandler = std::function<void(const muduo::net::TcpConnectionPtr& conn, nlohmann::json& js,
                                      muduo::Timestamp time)>;

class Service {
public:
    static Service* instance();
    MsgHandler GetHandler(int msgType);
    void ClientCloseException(const muduo::net::TcpConnectionPtr& conn);

    void Login(const muduo::net::TcpConnectionPtr& conn, nlohmann::json& js, muduo::Timestamp time);

    void Register(const muduo::net::TcpConnectionPtr& conn, nlohmann::json& js,
                  muduo::Timestamp time);
    void GetFriendInfo(const muduo::net::TcpConnectionPtr& conn, nlohmann::json& js,
                       muduo::Timestamp time);
    void AddFriendReq(const muduo::net::TcpConnectionPtr& conn, nlohmann::json& js,
                      muduo::Timestamp time);
    void AddFriendAck(const muduo::net::TcpConnectionPtr& conn, nlohmann::json& js,
                      muduo::Timestamp time);
    void oneChat(const muduo::net::TcpConnectionPtr& conn, nlohmann::json& js,
                 muduo::Timestamp time);

    void HandleRedisSubscribeMessage(int64_t userid, const std::string& msg);

private:
    Service();  // 构造函数私有化，禁止外部创建

    bool SendJson(const muduo::net::TcpConnectionPtr& conn, const nlohmann::json& response);
    bool GetAuthenticatedUserId(const muduo::net::TcpConnectionPtr& conn, int64_t& userId);

private:
    std::unordered_map<E_MSG_TYPE, MsgHandler> m_msgHandlerMap;
    std::unordered_map<int64_t, muduo::net::TcpConnectionPtr> m_userConnMap;
    std::unordered_map<const muduo::net::TcpConnection*, int64_t> m_connUserMap;

    std::mutex m_connMtx;

    // 数据操作类对象
    UserModel m_userModel;
    UserChangeModel m_userChangeModel;
    OfflineMsgModel m_offlineMsgModel;
    FriendModel m_friendModel;

    // redis操作对象
    Redis m_redis;

    RegistrationService m_registrationService;
};

#endif