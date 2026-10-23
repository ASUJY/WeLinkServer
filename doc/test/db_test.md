# 测试用例
| 是否通过 |        类名        |         被测试函数          | 测试功能                                                         |                 测试用例名称                  |
| :------: | :----------------: | :-------------------------: | :--------------------------------------------------------------- | :-------------------------------------------: |
|    是    | SnowflakeGenerator |                             | 验证正常生成的 ID 为正数，且连续生成的 ID 递增                   |               GenerateNormalId                |
|    是    | SnowflakeGenerator |                             | 验证 worker ID 超出允许范围时会抛出 std::invalid_argument。      |             WorkerIdOverflowThrow             |
|    是    | SnowflakeGenerator |                             | 验证数据中心 ID 超出允许范围时会抛出 std::invalid_argument       |           DatacenterIdOverflowThrow           |
|    是    | SnowflakeGenerator |                             | 验证 worker ID 和数据中心 ID 都取最大合法值时，仍能生成正数 ID。 |     AcceptsMaximumWorkerAndDatacenterIds      |
|    是    | SnowflakeGenerator |                             | 验证两个线程并发生成 2000 个 ID 时没有重复。                     |            MultiThreadNoDuplicate             |
|    是    |     UserModel      |           Insert            | 能写入 im_user 表                                                | UserCanBeInsertedAndLookedUpByNicknameOrPhone |
|    是    |     UserModel      |        QueryAccount         | 能分别按昵称和手机号查到用户，并核对 ID、昵称、手机号和密码。    | UserCanBeInsertedAndLookedUpByNicknameOrPhone |
|    是    |     UserModel      |        QueryUserInfo        | im_user表能返回用户 ID、手机号和昵称                             | UserCanBeInsertedAndLookedUpByNicknameOrPhone |
|    是    |     UserModel      | QueryAccount，QueryUserInfo | 查询不存在的用户时，返回失败或空结果。                           | UserCanBeInsertedAndLookedUpByNicknameOrPhone |
|    是    |  UserChangeModel   |           Insert            | 用户在线状态能插入                                               |    UserStateCanBeInsertedQueriedAndUpdated    |
|    是    |  UserChangeModel   |         QueryState          | 用户在线状态能查询                                               |    UserStateCanBeInsertedQueriedAndUpdated    |
|    是    |  UserChangeModel   |         UpdateState         | 用户在线状态能更改                                               |    UserStateCanBeInsertedQueriedAndUpdated    |
|    是    |  OfflineMsgModel   |           Insert            | 离线消息能写入                                                   |   OfflineMessagesAreIsolatedAndCanBeRemoved   |
|    是    |  OfflineMsgModel   |           Insert            | 离线消息能写入，消息含引号、反斜杠和空格时仍能保存               |   OfflineMessagesAreIsolatedAndCanBeRemoved   |
|    是    |  OfflineMsgModel   |           Insert            | 离线消息能写入，不同用户的消息互相隔离                           |   OfflineMessagesAreIsolatedAndCanBeRemoved   |
|    是    |  OfflineMsgModel   |            Query            | 离线消息能查询                                                   |   OfflineMessagesAreIsolatedAndCanBeRemoved   |
|    是    |  OfflineMsgModel   |           Remove            | 离线消息能删除                                                   |   OfflineMessagesAreIsolatedAndCanBeRemoved   |
|    是    |  OfflineMsgModel   |           Remove            | 删除一个用户的离线消息不会影响另一个用户。                       |   OfflineMessagesAreIsolatedAndCanBeRemoved   |

