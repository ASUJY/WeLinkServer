#include "friendservice.h"

#include <muduo/base/Logging.h>

#include <string>
#include <vector>

FriendService::FriendService(UserModel& userModel, FriendModel& friendModel,
                             std::function<void(int64_t, const nlohmann::json&)> deliver)
    : m_userModel(userModel), m_friendModel(friendModel), m_deliver(std::move(deliver)) {}

FriendService::Json FriendService::GetFriendInfo(const Json& request) {
    Json response;
    response["msgtype"] = E_MSG_TYPE::GET_FRIEND_INFO_ACK;

    if (!ValidateGetFriendInfo(request)) {
        response["data"]["errtype"] = E_ERR_TYPE::REQUEST_DATA_ERROR;
        return response;
    }

    try {
        std::map<std::string, std::string> data = request["data"];
        const auto accountType = static_cast<E_ACCOUNT_TYPE>(std::stoi(data.at("accountType")));
        const std::vector<std::string> result =
            m_userModel.QueryUserInfo(data["username"], accountType);
        if (!result.empty()) {
            response["data"]["userid"]   = result[0];
            response["data"]["phone"]    = result[1];
            response["data"]["username"] = result[2];
            response["data"]["errtype"]  = E_ERR_TYPE::GET_FRIEND_INFO_SUCCESS;
        } else {
            response["data"]["errtype"] = E_ERR_TYPE::GET_FRIEND_INFO_FAILED;
            LOG_INFO << "获取朋友失败";
        }
    } catch (const std::exception& e) {
        response["data"]["errtype"] = E_ERR_TYPE::GET_FRIEND_INFO_FAILED;
        LOG_WARN << "Invalid friend search request: " << e.what();
    }

    return response;
}

FriendService::Json FriendService::AddFriendReq(int64_t userId, const Json& request) {
    // 需要添加request校验
    try {
        nlohmann::json response;
        std::map<std::string, std::string> data = request["data"];

        const int64_t friendId           = stoll(data["friendid"]);
        response["data"]["senderid"]     = userId;
        response["data"]["sendername"]   = data["username"];
        response["data"]["receiverid"]   = friendId;
        response["data"]["receivername"] = data["friendname"];
        response["msgtype"]              = E_MSG_TYPE::ADD_FRIEND_REQ;
        m_deliver(friendId, response);
    } catch (const std::exception& e) {
        LOG_WARN << "Invalid friend request: " << e.what();
    }
}

auto FriendService::AddFriendAck(int64_t userId, const Json& request) -> Json {
    // 需要添加request校验
    try {
        nlohmann::json response;

        std::map<std::string, std::string> data = request["data"];
        const int64_t friendId                  = std::stoll(data["friendid"]);
        const int ackType                       = std::stoi(data["ackType"]);
        response["data"]["senderid"]            = userId;
        response["data"]["sendername"]          = data["username"];
        response["data"]["receiverid"]          = friendId;
        response["data"]["receivername"]        = data["friendname"];
        response["data"]["ackType"]             = ackType;
        response["msgtype"]                     = E_MSG_TYPE::ADD_FRIEND_ACK;

        LOG_INFO << "Friend reply: userid " << userId << " friendid: " << friendId
                 << " status: " << ackType;

        if (static_cast<FriendState>(ackType) == FriendState::ACCEPT) {
            m_friendModel.Insert(userId, friendId);
            m_friendModel.Insert(friendId, userId);
        }

        m_deliver(friendId, response);

    } catch (const std::exception& e) {
        LOG_WARN << "Invalid friend reply: " << e.what();
    }
}

bool FriendService::ValidateGetFriendInfo(const Json& request) {
    if (!request.is_object() || !request.contains("data") || !request.at("data").is_object()) {
        return false;
    }

    const auto& data = request.at("data");
    if (!ReadRequiredString(data, "userid", 64) || !ReadRequiredString(data, "username", 64) ||
        !ReadRequiredString(data, "phone", 11)) {
        return false;
    }
}

bool FriendService::ReadRequiredString(const Json& request, const std::string& key,
                                       size_t maxBytes) {
    if (!request.is_object() || !request.contains(key) || !request.at(key).is_string()) {
        return false;
    }

    const auto& field = request.at(key).get_ref<const std::string&>();
    if (field.empty() || field.size() > maxBytes) {
        return false;
    }

    return true;
}