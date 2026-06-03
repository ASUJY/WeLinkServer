#include "server.h"

int main() {

    muduo::net::EventLoop loop;

    // 定义监听地址：本地 127.0.0.1，端口 6000
    muduo::net::InetAddress addr("127.0.0.1", 6000);
    Server server(&loop, addr, "WeLinkServer");
    server.start();

    // 启动事件循环（阻塞等待事件：连接、消息、断开）
    loop.loop();

    return 0;
}