#pragma once

#include "friendmodel.h"
#include "friendrequestmodel.h"
#include "json.hpp"
#include "usermodel.h"

class FriendService {
    using Json = nlohmann::json;

public:
    FriendService(UserModel& userModel, FriendModel& friendModel,
                  std::function<void(int64_t, const Json&)> m_deliver);
    auto GetFriendInfo(const Json& request) -> Json;
    void AddFriendReq(int64_t userId, const Json& request);
    void AddFriendAck(int64_t userId, const Json& request);

private:
    bool ValidateGetFriendInfo(const Json& request);
    bool ReadRequiredString(const Json& request, const std::string& key, size_t maxBytes);

private:
    UserModel& m_userModel;
    FriendModel& m_friendModel;
    std::unique_ptr<FriendRequestModel> m_friendRequestModel;

    std::function<void(int64_t, const Json&)> m_deliver;
};