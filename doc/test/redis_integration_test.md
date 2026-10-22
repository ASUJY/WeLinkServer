# 测试用例
| 是否通过 | 测试功能                                                                                                           | 测试用例名称                                                   |
| :------: | :----------------------------------------------------------------------------------------------------------------- | :------------------------------------------------------------- |
|    是    | 连接Redis服务器                                                                                                    | RedisIntegrationTest.PublishSubscribeAndUnsubscribe            |
|    是    | 连接Redis服务器，向Redis服务器订阅频道                                                                             | RedisIntegrationTest.PublishSubscribeAndUnsubscribe            |
|    是    | 连接Redis服务器，向Redis服务器发布消息，消息内容（包括空格）能到达订阅端                                           | RedisIntegrationTest.PublishSubscribeAndUnsubscribe            |
|    是    | 连接Redis服务器，向Redis服务器发布消息，包含空字符串及带空格、引号、反斜杠等文本消息，能够正常的接收和发送这类消息 | RedisIntegrationTest.HandlesEmptyAndSpecialTextMessages        |
|    是    | 连接Redis服务器，Redis类子线程能收到Redis服务器发送过来的消息并调用回调函数，传递正确的频道号和内容。              | RedisIntegrationTest.PublishSubscribeAndUnsubscribe            |
|    是    | 连接Redis服务器，向Redis服务器取消订阅频道，后续Redis类子线程不会再接收到发布到这个频道的消息                      | RedisIntegrationTest.PublishSubscribeAndUnsubscribe            |
|    是    | 未连接redis服务器时发布消息、订阅频道和取消订阅频道应失败                                                          | RedisIntegrationTest.OperationsFailBeforeConnect               |
|    是    | 连接Redis服务器，redis服务器发送的消息到达redis客户端(本程序)期间替换或清空redis回调函数是否会有问题               | RedisIntegrationTest.CallbackCanBeReplacedWhileMessagesArrive  |
|    是    | 有活动的订阅连接时析构Redis对象，并检查析构耗时                                                                    | DestructorStopsObserverWithActiveSubscription                  |
|    是    | 连接Redis服务器，并发的向redis服务器发布消息（多线程并发发布），并检查所有消息是否到达订阅了消息的客户端           | RedisIntegrationTest.ConcurrentPublishCallsDeliverEveryMessage |
