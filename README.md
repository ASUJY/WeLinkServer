# WeLinkServer
> 基于C++/Muduo开发的即时通讯服务器程序，是WeLink项目的后端程序。  
> 
> 项目提供用户注册与登录、好友信息与好友请求、单聊消息转发，以及离线消息存储和上线后投递的基础能力。

![Language](https://img.shields.io/badge/language-C++-blue.svg)
![Muduo](https://img.shields.io/badge/network-Muduo-green.svg)
![MySQL](https://img.shields.io/badge/db-MySQL-lightblue.svg)
![Build](https://img.shields.io/badge/build-CMake-orange.svg)
![GTest](https://img.shields.io/badge/test-GTest-orange.svg)


# 构建
**构建需要以下组件：**
- CMake 3.16
- Muduo（muduo_net、muduo_base）
- MySQL
- hiredis
- GoogleTest 1.14.0（仅构建测试时；CMake 会通过 FetchContent 获取）

**运行需要以下服务：**
- MySQL服务
- Redis服务

# 模块说明
> 典型请求处理路径为：网络层读取并解析帧 → 业务层按消息类型分发 → 模型访问 MySQL 或 Redis → 网络层向客户端发送 JSON 响应。

|      模块      | 位置                                 | 职责                                                                              |
| :------------: | :----------------------------------- | :-------------------------------------------------------------------------------- |
| 程序入口与日志 | `src/main.cpp`                       | 配置 Muduo 异步日志，读取监听端口并启动事件循环和服务器。                         |
|     网络层     | `src/network/`                       | 管理 TCP 监听、连接生命周期和收发；解析长度头与 JSON 消息，并将消息交给业务层。   |
|   业务服务层   | `src/service/`                       | 按 `msgtype` 分发注册、登录、好友、聊天等请求；维护在线连接并协调数据库与 Redis。 |
|  用户数据模型  | `src/model/usermodel.cpp`、          | 创建用户、按账号类型查询账户及用户信息。                                          |
|  用户状态模型  | `src/model/userchangemodel.cpp`      | 查询和更新用户在线/离线状态。                                                     |
|    好友模型    | `src/model/friendmodel.cpp`          | 新增好友关系并查询好友列表。                                                      |
|  离线消息模型  | `src/model/offlinemessagemodel.cpp`  | 保存、查询并清理用户的离线消息。                                                  |
|  MySQL 访问层  | `src/db/mysqldb.cpp`                 | 封装 MySQL 连接、查询、更新及预处理更新接口。                                     |
|  Redis 消息层  | `src/redis/redis.cpp`                | 使用 Redis Pub/Sub 在服务端实例间转发在线消息，并通过订阅回调通知业务层。         |
|   ID 生成器    | `src/db/snowflake.cpp`               | 生成并发安全的 Snowflake 64 位 ID。                                               |
| 公共协议与模型 | `include/common.h`、`include/model/` | 定义消息/错误类型、好友状态及用户数据结构。                                       |
|      测试      | `test/`                              | 包含 Snowflake 单元测试及可选的 Redis 集成测试。                                  |
|    运维脚本    | `scripts/`                           | 提供 `WeLinkServer` 进程监控和钉钉告警脚本。                                      |



# 开发计划
> 详情可以查看以下文档，包含了已实现的功能描述，和即将开发的功能描述  
> 功能概览：  
> - 使用 Muduo 网络库接收 TCP 连接，并以固定长度头封装 JSON 消息，支持处理 TCP 粘包和半包。  
> - 支持用户注册、登录和在线状态更新。  
> - 支持好友信息查询、好友请求及处理。  
> - 支持单聊：在线用户通过 Redis Pub/Sub 转发，离线用户的消息写入 MySQL，并在下次登录后投递。   
> - 使用 Muduo 异步日志写入服务日志。  

[业务模块](doc/service.md)  
[网络通信模块](doc/redis.md)  
[数据库模块](doc/db.md)  
[Redis模块](doc/redis.md)  
[日志模块](doc/log.md)  
