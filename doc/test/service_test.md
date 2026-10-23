# 测试用例
| 是否通过 |     业务     | 测试功能                                                                                                                         |                         测试用例名称                         |
| :------: | :----------: | :------------------------------------------------------------------------------------------------------------------------------- | :----------------------------------------------------------: |
|    是    |   注册业务   | 验证能否注册                                                                                                                     |             RegistrationLoginAndOfflineMessages              |
|    是    |   登录业务   | 错误密码拒绝登录                                                                                                                 |             RegistrationLoginAndOfflineMessages              |
|    是    |   登录业务   | 正确密码登录成功                                                                                                                 |             RegistrationLoginAndOfflineMessages              |
|    是    |   登录业务   | 登录成功，会收到离线消息，离线消息随后会从数据库清除                                                                             |             RegistrationLoginAndOfflineMessages              |
|    是    |   登录业务   | 登录成功，用户状态变为在线状态                                                                                                   |             RegistrationLoginAndOfflineMessages              |
|    是    |   登录业务   | 同一用户再次登录会被拒绝                                                                                                         |             RegistrationLoginAndOfflineMessages              |
|    是    |   登录业务   | 两个连接同时用同一账号登录，预期只有一个登录成功，另一个收到“用户已在线”，数据库中的用户状态为在线。                             |            ConcurrentLoginAllowsOnlyOneConnection            |
|    是    |   登录业务   | 登录成功后模拟连接关闭，检查用户状态是否从在线恢复为离线。                                                                       |          ClosingLoggedInConnectionMarksUserOffline           |
|    是    |   登录业务   | 使用手机号和手机号账号类型登录，检查登录成功且返回的用户 ID 正确。                                                               |               LoginByPhoneUsesPhoneAccountType               |
|    否    |   登录业务   | 发送 accountType 为非数字的登录请求，检查字段转换异常不会逃出消息处理函数。                                                      |        MalformedLoginAccountTypeDoesNotEscapeHandler         |
|    是    | 好友添加业务 | 在线用户能收到添加好友申请                                                                                                       |      FriendRequestsAndChatsRouteToOnlineOrOfflineUsers       |
|    是    | 好友添加业务 | 在线用户接受好友请求后好友关系写入数据库                                                                                         |      FriendRequestsAndChatsRouteToOnlineOrOfflineUsers       |
|    是    | 好友添加业务 | 离线用户的添加好友申请会存入离线消息表                                                                                           |      FriendRequestsAndChatsRouteToOnlineOrOfflineUsers       |
|    是    | 好友添加业务 | 未登录连接提交带有伪造发送者 ID 的好友请求，预期已登录的接收者收不到请求，离线消息库中也不会出现该请求。                         |   UnauthenticatedConnectionCannotSpoofFriendRequestSender    |
|    否    | 好友添加业务 | 申请方先发送好友申请，再下线；接收方接受申请后，双方好友关系仍应写入数据库。                                                     | AcceptingFriendRequestPersistsRelationWhenRequesterIsOffline |
|    否    | 好友添加业务 | 没有待处理申请时，伪造“接受”响应不能创建好友关系。                                                                               |      FriendAckCannotCreateRelationWithoutPendingRequest      |
|    是    | 好友添加业务 | 未登录连接不能提交好友申请响应或建立好友关系。                                                                                   |        UnauthenticatedConnectionCannotSubmitFriendAck        |
|    是    |   单聊业务   | 在线用户能收到聊天消息                                                                                                           |      FriendRequestsAndChatsRouteToOnlineOrOfflineUsers       |
|    是    |   单聊业务   | 离线用户的聊天消息会存入离线消息表                                                                                               |      FriendRequestsAndChatsRouteToOnlineOrOfflineUsers       |
|    是    |   单聊业务   | 未登录连接不能向在线用户发送伪造聊天消息。(未登录连接向在线用户发送聊天消息，检查接收方既收不到消息，离线消息库也没有新增记录。) |           UnauthenticatedConnectionCannotSendChat            |
|    是    |   单聊业务   | 登录用户发送聊天消息时伪造 senderId，检查转发消息中的发送方 ID 被替换为该连接实际登录用户的 ID。                                 |         ChatSenderIdComesFromAuthenticatedConnection         |
|    是    |  跨服务聊天  | 用户状态显示在线但当前服务没有该用户连接时，聊天消息会通过 Redis Pub/Sub 转发。                                                  |      FriendRequestsAndChatsRouteToOnlineOrOfflineUsers       |