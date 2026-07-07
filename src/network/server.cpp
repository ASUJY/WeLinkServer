#include "server.h"

#include <muduo/base/Logging.h>

#include <functional>
#include <iostream>
#include <string>

#include "common.h"
#include "service.h"

Server::Server(muduo::net::EventLoop* loop, const muduo::net::InetAddress& listenAddr,
               const std::string& nameArg)
    : m_server(loop, listenAddr, nameArg), m_loop(loop) {
    // 注册【连接回调】（客户端上线/下线自动执行回调函数）
    m_server.setConnectionCallback(std::bind(&Server::OnConnection, this, std::placeholders::_1));
    // 注册【消息回调】（客户端发消息自动执行回调函数）
    m_server.setMessageCallback(std::bind(&Server::OnMessage, this, std::placeholders::_1,
                                          std::placeholders::_2, std::placeholders::_3));

    // 设置服务器线程数量(1 个 main reactor 线程（负责接收新连接）,3 个 sub reactor
    // 线程（负责已连接客户端的 IO）)
    // 可优化，自动根据系统硬件配置设置线程数量，或者考虑做成配置项(通常 I/O 线程数 = CPU 核心数)
    m_server.setThreadNum(4);
}

void Server::start() {
    m_server.start();  // 启动服务(开始监听端口、接受连接、运行事件循环)
}

void Server::OnConnection(const muduo::net::TcpConnectionPtr& conn) {
    if (!conn->connected()) {
        Service::instance()->ClientCloseException(conn);
        // conn->shutdown();
    }
}

void Server::OnMessage(const muduo::net::TcpConnectionPtr& conn, muduo::net::Buffer* buffer,
                       muduo::Timestamp time) {
    // 可优化地方，需要能够处理粘包半包的情况
    std::string buf = buffer->retrieveAllAsString();
    nlohmann::json js;
    try {
        js = nlohmann::json::parse(buf);
    } catch (const nlohmann::json::exception& e) {
        LOG_ERROR << "JSON parse error: " << e.what();
        conn->shutdown();
        return;
    }

    auto msgHandler = Service::instance()->GetHandler(js["msgtype"].get<E_MSG_TYPE>());
    msgHandler(conn, js, time);
}