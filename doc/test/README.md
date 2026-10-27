# 测试说明

## 构建与运行

```bash
cmake -S . -B build -DBUILD_TESTS=ON
cmake --build build -j
ctest --test-dir build --output-on-failure

# 列出所有测试组
./bin/run_all_test --gtest_list_tests
# 单独执行某个测试组
./bin/run_all_test --gtest_filter='LoginIntegrationTest.*'
```

Snowflake 和协议测试不需要外部服务。

Redis 与 MySQL 集成测试默认跳过；显式设置环境变量后再运行：
```bash
WE_LINK_REDIS_INTEGRATION=1 ctest --test-dir build --output-on-failure
WE_LINK_MYSQL_INTEGRATION=1 ctest --test-dir build --output-on-failure
```
- Redis 和 MySQL 集成测试读取项目的服务配置。先将 `config.example.json` 复制为 `config.json` 并填写可用的连接参数；也可通过 `WE_LINK_CONFIG` 指定配置文件路径。
- MySQL 测试要求数据库已有 `offlinemessage(userid, message)` 和 `im_userchange(userid, state)` 表。测试使用独立的大 ID 并在结束时清理记录。

## 当前覆盖
![网络通信模块测试](./network_test.md)
![redis模块测试](./redis_integration_test.md)
![数据库模块测试](./mysql_test.md)