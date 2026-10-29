| 是否通过 |                                                             测试功能                                                              | 测试用例名称                                  |
| :------: | :-------------------------------------------------------------------------------------------------------------------------------: | :-------------------------------------------- |
|    是    |                                         用户名和正确密码，登录成功，用户状态变为在线状态                                          | RegistrationLoginAndOfflineMessages           |
|    是    |                                                         错误密码拒绝登录                                                          | RegistrationLoginAndOfflineMessages           |
|    是    |                               登录成功，如果有离线消息，会收到离线消息，离线消息随后会从数据库清除                                | RegistrationLoginAndOfflineMessages           |
|    是    |                                                     同一用户再次登录会被拒绝                                                      | RegistrationLoginAndOfflineMessages           |
|    是    |                             测试指定“手机号”账号类型时，能用手机号和正确密码登录，并返回对应用户 ID。                             | LoginByPhoneUsesPhoneAccountType              |
|    是    | 发送 accountType 为非数字的登录请求，检查字段转换异常不会逃出消息处理函数(测试账号类型是非数字字符串时，处理函数不会抛出异常。)。 | MalformedLoginAccountTypeDoesNotEscapeHandler |
|    是    |               两个连接同时用同一账号登录，预期只有一个登录成功，另一个收到“用户已在线”，数据库中的用户状态为在线。                | ConcurrentLoginAllowsOnlyOneConnection        |
|    是    |                                    登录成功后模拟连接关闭，检查用户状态是否从在线恢复为离线。                                     | ClosingLoggedInConnectionMarksUserOffline     |
|    是    |                                                 MySQL不可用时，登录应返回失败响应                                                 | MysqlUnavailableReturnsLoginFailure           |
