# 测试用例
| 是否通过 | 测试功能                                                                              |                 测试用例名称                 |
| :------: | :------------------------------------------------------------------------------------ | :------------------------------------------: |
|    是    | 验证有效配置能正确读取 MySQL、Redis 的字段，Redis 未填写 password 时默认为空字符串。  | LoadsRequiredFieldsAndDefaultsRedisPassword  |
|    是    | 验证配置文件不存在时会抛出 std::runtime_error                                         |              RejectsMissingFile              |
|    是    | 验证 JSON 格式错误，以及缺少必填配置字段时会抛出异常                                  | RejectsMalformedJsonAndMissingRequiredFields |
|    是    | 验证端口不能是字符串，MySQL 端口不能小于 1 或大于 65535，Redis 端口也必须在有效范围内 |       RejectsInvalidPortTypesAndRanges       |