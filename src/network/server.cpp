#include "server.h"
#include <functional>
#include <string>

Server::Server(muduo::net::EventLoop* loop, 
    const muduo::net::InetAddress& listenAddr, 
    const std::string& nameArg) : m_server(loop, listenAddr, nameArg), m_loop(loop) {
    
    // 注册【连接回调】（客户端上线/下线自动执行回调函数）
    m_server.setConnectionCallback(std::bind(&Server::OnConnection, this, std::placeholders::_1));
    // 注册【消息回调】（客户端发消息自动执行回调函数）
    m_server.setMessageCallback(std::bind(&Server::OnMessage, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3));
    // 设置服务器线程数量(1 个 main reactor 线程（负责接收新连接）,3 个 sub reactor 线程（负责已连接客户端的 IO）)
    m_server.setThreadNum(4);
}

void Server::start() {
    m_server.start();   // 启动服务(开始监听端口、接受连接、运行事件循环)
}

void Server::OnConnection(const muduo::net::TcpConnectionPtr &conn) {
    if (!conn->connected()) {
        conn->shutdown();
    }
}

void Server::OnMessage(const muduo::net::TcpConnectionPtr &conn, muduo::net::Buffer *buffer, muduo::Timestamp time) {
    std::string buf = buffer->retrieveAllAsString();

}