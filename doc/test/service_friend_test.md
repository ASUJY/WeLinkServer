| 是否通过 | 测试功能                                                                                                 |                         测试用例名称                         |
| :------: | :------------------------------------------------------------------------------------------------------- | :----------------------------------------------------------: |
|    是    | 在线用户能收到添加好友申请                                                                               |      FriendRequestsAndChatsRouteToOnlineOrOfflineUsers       |
|    是    | 在线用户接受好友请求后好友关系写入数据库                                                                 |      FriendRequestsAndChatsRouteToOnlineOrOfflineUsers       |
|    是    | 离线用户的添加好友申请不会存入离线消息表；离线单聊消息仍会存入离线消息表                                 |      FriendRequestsAndChatsRouteToOnlineOrOfflineUsers       |
|    是    | 未登录连接提交带有伪造发送者 ID 的好友请求，预期已登录的接收者收不到请求，离线消息库中也不会出现该请求。 |   UnauthenticatedConnectionCannotSpoofFriendRequestSender    |
|    否    | 申请方先发送好友申请，再下线；接收方接受申请后，双方好友关系仍应写入数据库。                             | AcceptingFriendRequestPersistsRelationWhenRequesterIsOffline |
|    否    | 没有待处理申请时，伪造“接受”响应不能创建好友关系。                                                       |      FriendAckCannotCreateRelationWithoutPendingRequest      |
|    是    | 未登录连接不能提交好友申请响应或建立好友关系。                                                           |        UnauthenticatedConnectionCannotSubmitFriendAck        |