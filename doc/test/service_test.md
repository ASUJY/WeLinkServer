# 测试用例
| 是否通过 |    业务    | 测试功能                                                                                                                         |                   测试用例名称                    |
| :------: | :--------: | :------------------------------------------------------------------------------------------------------------------------------- | :-----------------------------------------------: |
|    是    |  单聊业务  | 在线用户能收到聊天消息                                                                                                           | FriendRequestsAndChatsRouteToOnlineOrOfflineUsers |
|    是    |  单聊业务  | 离线用户的聊天消息会存入离线消息表                                                                                               | FriendRequestsAndChatsRouteToOnlineOrOfflineUsers |
|    是    |  单聊业务  | 未登录连接不能向在线用户发送伪造聊天消息。(未登录连接向在线用户发送聊天消息，检查接收方既收不到消息，离线消息库也没有新增记录。) |      UnauthenticatedConnectionCannotSendChat      |
|    是    |  单聊业务  | 登录用户发送聊天消息时伪造 senderId，检查转发消息中的发送方 ID 被替换为该连接实际登录用户的 ID。                                 |   ChatSenderIdComesFromAuthenticatedConnection    |
|    是    | 跨服务聊天 | 用户状态显示在线但当前服务没有该用户连接时，聊天消息会通过 Redis Pub/Sub 转发。                                                  | FriendRequestsAndChatsRouteToOnlineOrOfflineUsers |