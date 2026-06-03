#ifndef SERVER_H
#define SERVER_H

#include <muduo/net/TcpServer.h>
#include <muduo/net/EventLoop.h>

/**
 * 只负责网络通信(接收客户端连接，收发数据)
*/


class Server {
public:
    Server(muduo::net::EventLoop* loop, 
    const muduo::net::InetAddress& listenAddr, 
    const std::string& nameArg);

    void start();

private:
    // 处理客户端连接/断开
    void OnConnection(const muduo::net::TcpConnectionPtr &conn);
    // 处理客户端发来的消息
    void OnMessage(const muduo::net::TcpConnectionPtr &conn, 
                    muduo::net::Buffer *buffer, 
                    muduo::Timestamp time);

private:
    muduo::net::TcpServer m_server;
    muduo::net::EventLoop *m_loop;  // 事件循环（驱动整个网络IO）
};

#endif