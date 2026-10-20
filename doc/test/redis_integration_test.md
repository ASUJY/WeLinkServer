# 测试用例
**已通过的测试用例：**
- RedisIntegrationTest.PublishSubscribeAndUnsubscribe
  - Connect() 能建立发布和订阅连接。
  - Subscribe() 能订阅频道。
  - Publish() 能发布消息，消息内容（包括空格）能到达订阅端。
  - 观察线程能收到消息并调用回调，传递正确的频道号和内容。
  - Unsubscribe() 后，后续发布的消息不会再触发回调。

- 未连接redis服务器时发布消息、订阅频道和取消订阅频道应失败：RedisIntegrationTest.OperationsFailBeforeConnect
- 空字符串及带空格、引号、反斜杠等文本消息的接收和发送：RedisIntegrationTest.HandlesEmptyAndSpecialTextMessages
- redis服务器发送的消息到达redis客户端(本程序)期间替换或清空redis回调函数是否会有问题：RedisIntegrationTest.CallbackCanBeReplacedWhileMessagesArrive
- 有活动订阅时析构Redis对象，并检查析构耗时：DestructorStopsObserverWithActiveSubscription

**未通过的测试用例**
- 并发的向redis服务器发布消息（多线程并发发布），并检查所有消息是否到达订阅了消息的客户端：RedisIntegrationTest.ConcurrentPublishCallsDeliverEveryMessage