#include <muduo/base/AsyncLogging.h>
#include <muduo/base/Logging.h>

#include "server.h"

muduo::AsyncLogging* g_asyncLog = nullptr;
// 日志输出回调函数：把日志交给异步日志写入文件
void asyncOutput(const char* msg, int len) { g_asyncLog->append(msg, len); }

int main(int argc, char** argv) {
    // ============== 1. 配置异步日志参数 ==============
    // 参数1：日志文件名前缀（会自动生成 xxx.log、xxx.1.log）
    // 参数2：单个日志文件最大大小（默认 50MB = 50*1000*1000）
    // 参数3：刷盘间隔（默认 3秒，后台线程每3秒写一次文件）
    muduo::AsyncLogging log("muduo_server_log", 50 * 1000 * 1000, 3);
    g_asyncLog = &log;
    // ============== 2. 替换默认输出函数 ==============
    muduo::Logger::setOutput(asyncOutput);
    // ============== 3. 启动异步日志线程 ==============
    log.start();

    if (argc < 2) {
        LOG_ERROR << "command invalid! example: ./WeLinkServer 6000";
        exit(-1);
    }

    muduo::net::EventLoop loop;

    // 定义监听地址：本地 127.0.0.1，端口 9999
    muduo::net::InetAddress addr("0.0.0.0", atoi(argv[1]));
    Server server(&loop, addr, "WeLinkServer");
    server.start();

    // 启动事件循环（阻塞等待事件：连接、消息、断开）
    loop.loop();
    log.stop();
    return 0;
}