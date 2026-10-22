# 测试用例
| 是否通过 |    被测试函数    | 测试功能                                                                                                                             |                     测试用例名                     |
| :------: | :--------------: | :----------------------------------------------------------------------------------------------------------------------------------- | :------------------------------------------------: |
|          |   DecodeFrames   | 完整收到一帧时，能正确提取消息体，并且全部输入字节都已处理。                                                                         |              DecodesOneCompleteFrame               |
|          |   DecodeFrames   | 输入不足以组成完整帧时，包括长度头未收全或消息体未收全，返回 NEED_MORE，等待下一帧数据到来再进行处理（即不输出消息，也不消费输入）。 |            RetainsPartialHeaderAndBody             |
|          |   DecodeFrames   | 一次输入包含两帧完整消息和一帧不完整消息时，能提取前两帧，并保留尾部未完整帧的数据供后续读取。                                       | DecodesMultipleFramesAndLeavesTrailingPartialFrame |
|          |   DecodeFrames   | 拒绝长度为 0、负数或超过 1 MB 的帧头。                                                                                               |       RejectsZeroNegativeAndOversizedLengths       |
|          |   EncodeFrame    | 编码时拒绝空消息体和超过 1 MB 的消息体。                                                                                             |        EncodeRejectsEmptyAndOversizedBodies        |
|          |   EncodeFrame    | 编码出的长度头使用网络字节序，且后面紧跟原始消息体。                                                                                 |          EncodesLengthInNetworkByteOrder           |
|          | ParseJsonMessage | 只接受合法 JSON 对象，且必须包含整数类型的 msgtype；拒绝非法 JSON、缺少 msgtype、msgtype 类型错误或 JSON 顶层不是对象的内容。        |          ParsesOnlyValidMessageEnvelopes           |