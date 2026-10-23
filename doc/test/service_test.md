# 测试用例
| 是否通过 |     业务     | 测试功能                                                                                                 |                      测试用例名称                       |
| :------: | :----------: | :------------------------------------------------------------------------------------------------------- | :-----------------------------------------------------: |
|    是    |   注册业务   | 验证能否注册                                                                                             |           RegistrationLoginAndOfflineMessages           |
|    是    |   登录业务   | 错误密码拒绝登录                                                                                         |           RegistrationLoginAndOfflineMessages           |
|    是    |   登录业务   | 正确密码登录成功                                                                                         |           RegistrationLoginAndOfflineMessages           |
|    是    |   登录业务   | 登录成功，会收到离线消息，离线消息随后会从数据库清除                                                     |           RegistrationLoginAndOfflineMessages           |
|    是    |   登录业务   | 登录成功，用户状态变为在线状态                                                                           |           RegistrationLoginAndOfflineMessages           |
|    是    |   登录业务   | 同一用户再次登录会被拒绝                                                                                 |           RegistrationLoginAndOfflineMessages           |
|          |   登录业务   | 两个连接同时用同一账号登录，预期只有一个登录成功，另一个收到“用户已在线”，数据库中的用户状态为在线。     |         ConcurrentLoginAllowsOnlyOneConnection          |
|          |   登录业务   | 登录成功后模拟连接关闭，检查用户状态是否从在线恢复为离线。                                               |        ClosingLoggedInConnectionMarksUserOffline        |
|    是    | 好友添加业务 | 在线用户能收到添加好友申请                                                                               |    FriendRequestsAndChatsRouteToOnlineOrOfflineUsers    |
|    是    | 好友添加业务 | 在线用户接受好友请求后好友关系写入数据库                                                                 |    FriendRequestsAndChatsRouteToOnlineOrOfflineUsers    |
|    是    | 好友添加业务 | 离线用户的添加好友申请会存入离线消息表                                                                   |    FriendRequestsAndChatsRouteToOnlineOrOfflineUsers    |
|          | 好友添加业务 | 未登录连接提交带有伪造发送者 ID 的好友请求，预期已登录的接收者收不到请求，离线消息库中也不会出现该请求。 | UnauthenticatedConnectionCannotSpoofFriendRequestSender |
|    是    |   单聊业务   | 在线用户能收到聊天消息                                                                                   |    FriendRequestsAndChatsRouteToOnlineOrOfflineUsers    |
|    是    |   单聊业务   | 离线用户的聊天消息会存入离线消息表                                                                       |    FriendRequestsAndChatsRouteToOnlineOrOfflineUsers    |
|    是    |  跨服务聊天  | 用户状态显示在线但当前服务没有该用户连接时，聊天消息会通过 Redis Pub/Sub 转发。                          |    FriendRequestsAndChatsRouteToOnlineOrOfflineUsers    |