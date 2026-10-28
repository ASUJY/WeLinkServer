#include "chatservice.h"

#include <muduo/base/Logging.h>

ChatService::ChatService(std::function<void(int64_t, const Json&)> deliver)
    : m_deliver(std::move(deliver)) {}

void ChatService::SendOneChat(int64_t senderId, const Json& request) {
    if (!request.is_object() || !request.contains("data") || !request.at("data").is_object() ||
        !request.at("data").contains("receiverId")) {
        LOG_WARN << "Invalid one-to-one chat request";
        return;
    }

    int64_t receiverId = -1;

    try {
        const auto& receiver = request.at("data").at("receiverId");
        if (receiver.is_string()) {
            const std::string value = receiver.get<std::string>();
            size_t parsedLength     = 0;
            receiverId              = std::stoll(value, &parsedLength);
            if (parsedLength != value.size()) {
                LOG_WARN << "Invalid receiverId in one-to-one chat request";
                return;
            }
        } else if (receiver.is_number_integer()) {
            receiverId = receiver.get<int64_t>();
        } else {
            LOG_WARN << "Invalid receiverId in one-to-one chat request";
            return;
        }
    } catch (const std::exception& e) {
        LOG_WARN << "Invalid receiverId in one-to-one chat request: " << e.what();
        return;
    }

    if (receiverId <= 0) {
        LOG_WARN << "Invalid receiverId in one-to-one chat request";
        return;
    }

    Json message = request;
    // 客户端提供的发送方身份不可信，转发前用当前连接的登录身份覆盖。
    message["data"]["senderId"] = std::to_string(senderId);
    m_deliver(receiverId, message);
}