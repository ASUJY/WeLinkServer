# 测试用例
| 是否通过 |        被测试函数        | 测试功能                                                                                                                             |                     测试用例名                     |
| :------: | :----------------------: | :----------------------------------------------------------------------------------------------------------------------------------- | :------------------------------------------------: |
|    是    |       DecodeFrames       | 完整收到一帧时，能正确提取消息体，并且全部输入字节都已处理。                                                                         |              DecodesOneCompleteFrame               |
|    是    |       DecodeFrames       | 输入不足以组成完整帧时，包括长度头未收全或消息体未收全，返回 NEED_MORE，等待下一帧数据到来再进行处理（即不输出消息，也不消费输入）。 |            RetainsPartialHeaderAndBody             |
|    是    |       DecodeFrames       | 一次输入包含两帧完整消息和一帧不完整消息时，能提取前两帧，并保留尾部未完整帧的数据供后续读取。                                       | DecodesMultipleFramesAndLeavesTrailingPartialFrame |
|    是    |       DecodeFrames       | 拒绝长度为 0、负数或超过 1 MB 的帧头。                                                                                               |       RejectsZeroNegativeAndOversizedLengths       |
|    是    |       EncodeFrame        | 编码时拒绝空消息体和超过 1 MB 的消息体。                                                                                             |        EncodeRejectsEmptyAndOversizedBodies        |
|    是    | EncodeFrame,DecodeFrames | 验证大小恰好为 1 MB 的消息体可以成功编码并解码，内容和消费字节数正确。                                                               |         EncodesAndDecodesMaximumSizedBody          |
|    是    |       DecodeFrames       | 验证空输入被视为完整缓冲区，解码结果为 OK，且消费字节数为 0                                                                          |         EmptyInputIsAnEmptyCompleteBuffer          |
|    是    |       EncodeFrame        | 编码出的长度头使用网络字节序，且后面紧跟原始消息体。                                                                                 |          EncodesLengthInNetworkByteOrder           |
|    是    |     ParseJsonMessage     | 只接受合法 JSON 对象，且必须包含整数类型的 msgtype；拒绝非法 JSON、缺少 msgtype、msgtype 类型错误或 JSON 顶层不是对象的内容。        |          ParsesOnlyValidMessageEnvelopes           |
|    是    |     ParseJsonMessage     | 验证 JSON 中的 msgtype 为浮点数或 null，以及 JSON 顶层为 null 时，解析会失败。                                                       |           RejectsNonIntegerMessageTypes            |