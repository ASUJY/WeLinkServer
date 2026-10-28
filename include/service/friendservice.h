#pragma once

#include "friendmodel.h"
#include "json.hpp"
#include "usermodel.h"

class FriendService {
    using Json = nlohmann::json;

public:
    FriendService(UserModel& userModel, FriendModel& friendModel,
                  std::function<void(int64_t, const Json&)> m_deliver);
    auto GetFriendInfo(const Json& request) -> Json;
    auto AddFriendReq(int64_t userId, const Json& request) -> Json;
    auto AddFriendAck(int64_t userId, const Json& request) -> Json;

private:
    bool ValidateGetFriendInfo(const Json& request);
    bool ReadRequiredString(const Json& request, const std::string& key, size_t maxBytes);

private:
    UserModel& m_userModel;
    FriendModel& m_friendModel;

    std::function<void(int64_t, const Json&)> m_deliver;
};