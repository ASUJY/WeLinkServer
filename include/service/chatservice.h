#pragma once

#include <functional>

#include "friendmodel.h"
#include "json.hpp"
class ChatService {
    using Json = nlohmann::json;

public:
    explicit ChatService(FriendModel& friendModel,
                         std::function<void(int64_t, const Json&)> deliver);
    void SendOneChat(int64_t senderId, const Json& request);

private:
    std::function<void(int64_t, const Json&)> m_deliver;
    FriendModel& m_friendModel;
};