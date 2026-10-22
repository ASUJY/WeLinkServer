# 测试用例
| 是否通过 |      类名       | 被测试函数  | 测试功能                                           |               测试用例名称                |
| :------: | :-------------: | :---------: | :------------------------------------------------- | :---------------------------------------: |
|          | OfflineMsgModel |   Insert    | 离线消息能写入                                     | OfflineMessagesAreIsolatedAndCanBeRemoved |
|          | OfflineMsgModel |   Insert    | 离线消息能写入，消息含引号、反斜杠和空格时仍能保存 | OfflineMessagesAreIsolatedAndCanBeRemoved |
|          | OfflineMsgModel |   Insert    | 离线消息能写入，不同用户的消息互相隔离             | OfflineMessagesAreIsolatedAndCanBeRemoved |
|          | OfflineMsgModel |    Query    | 离线消息能查询                                     | OfflineMessagesAreIsolatedAndCanBeRemoved |
|          | OfflineMsgModel |   Remove    | 离线消息能删除                                     | OfflineMessagesAreIsolatedAndCanBeRemoved |
|          | OfflineMsgModel |   Remove    | 删除一个用户的离线消息不会影响另一个用户。         | OfflineMessagesAreIsolatedAndCanBeRemoved |
|          | UserChangeModel |   Insert    | 用户在线状态能插入                                 |  UserStateCanBeInsertedQueriedAndUpdated  |
|          | UserChangeModel | QueryState  | 用户在线状态能查询                                 |  UserStateCanBeInsertedQueriedAndUpdated  |
|          | UserChangeModel | UpdateState | 用户在线状态能更改                                 |  UserStateCanBeInsertedQueriedAndUpdated  |