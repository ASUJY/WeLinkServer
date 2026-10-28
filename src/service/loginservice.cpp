#include "loginservice.h"

#include <muduo/base/Logging.h>

#include <string>

LoginService::LoginService(UserModel& userModel, UserChangeModel& userChangeModel,
                           OfflineMsgModel& offlineMsgModel)
    : m_userModel(userModel),
      m_userChangeModel(userChangeModel),
      m_offlineMsgModel(offlineMsgModel) {}

LoginService::Json LoginService::Login(const Json& loginInfo,
                                       const SessionRegistrar& registerSession) {
    nlohmann::json response;
    response["msgtype"]  = E_MSG_TYPE::LOGIN_MSG_ACK;
    int accountTypeValue = -1;
    if (!ValidateLoginInfo(loginInfo, accountTypeValue)) {
        response["data"]["errtype"] = E_ERR_TYPE::ACCOUNT_ERROR;
        return response;
    }

    std::unordered_map<std::string, std::string> data = loginInfo["data"];
    User loginUser;
    loginUser.SetUserName(data["username"]);
    loginUser.SetPwd(data["pasword"]);

    const auto accountType = static_cast<E_ACCOUNT_TYPE>(accountTypeValue);
    if (!m_userModel.QueryAccount(loginUser, accountType)) {
        LOG_INFO << "login failed: account not found";
        response["data"]["errtype"] = E_ERR_TYPE::USER_NO_REGISTER;
        return response;
    }

    if (loginUser.GetPwd() != data["password"]) {
        LOG_INFO << "login failed: invalid password";
        response["data"]["errtype"] = E_ERR_TYPE::ACCOUNT_ERROR;
        return response;
    }

    const auto state = m_userChangeModel.QueryState(loginUser.GetUserId());
    if (state.empty()) {
        response["data"]["errtype"] = E_ERR_TYPE::USER_NO_REGISTER;
        return response;
    }

    if (state == "online" || !registerSession(loginUser)) {
        response["data"]["errtype"] = E_ERR_TYPE::USER_ONLINE;
        return response;
    }

    LOG_INFO << "login success: " << loginUser.GetUserName() << " " << loginUser.GetUserId();
    response["data"]["errtype"]  = E_ERR_TYPE::LOGIN_MSG_ACK_SUCCESS;
    response["data"]["userid"]   = loginUser.GetUserId();
    response["data"]["username"] = loginUser.GetUserName();
    response["data"]["phone"]    = loginUser.GetUserPhone();
    response["data"]["avator"]   = loginUser.GetUserAvatar();

    // 查询该用户是否有离线消息
    const std::vector<std::string> offlineMessages = m_offlineMsgModel.Query(loginUser.GetUserId());
    if (!offlineMessages.empty()) {
        response["offlinemsg"] = offlineMessages;
        m_offlineMsgModel.Remove(loginUser.GetUserId());
    }

    // 需要返回好友信息

    // 查询该用户的好友信息并返回，这里可以优化一下，看看是否能获取用户登录设备的信息
    // 如果是常用登录设备就不需要返回好友信息了

    return response;
}

bool LoginService::ValidateLoginInfo(const Json& loginInfo, int& accountTypeValue) {
    if (!loginInfo.is_object() || !loginInfo.contains("data") ||
        !loginInfo.at("data").is_object()) {
        return false;
    }

    const auto& data = loginInfo.at("data");
    if (!ReadRequiredString(data, "username", 64) || !ReadRequiredString(data, "password", 128) ||
        !ValidateAccountType(data, accountTypeValue)) {
        return false;
    }
}

bool LoginService::ReadRequiredString(const Json& loginInfo, const std::string& key,
                                      size_t maxBytes) {
    if (!loginInfo.is_object() || !loginInfo.contains(key) || !loginInfo.at(key).is_string()) {
        return false;
    }

    const auto& field = loginInfo.at(key).get_ref<const std::string&>();
    if (field.empty() || field.size() > maxBytes) {
        return false;
    }

    return true;
}

bool LoginService::ValidateAccountType(const Json& loginInfo, int& accountTypeValue) {
    if (!loginInfo.is_object() || !loginInfo.contains("accountType")) {
        return false;
    }

    try {
        const auto& accountType = loginInfo.at("accountType");
        if (accountType.is_string()) {
            const std::string value = accountType.get<std::string>();
            size_t parsedBytes      = 0;
            accountTypeValue        = std::stoi(value, &parsedBytes);
            if (parsedBytes != value.size()) {
                return false;
            }
        } else if (accountType.is_number_integer()) {
            accountTypeValue = accountType.get<int>();
        } else {
            return false;
        }
    } catch (const std::exception&) {
        return false;
    }

    if (accountTypeValue != static_cast<int>(E_ACCOUNT_TYPE::NICKNAME) &&
        accountTypeValue != static_cast<int>(E_ACCOUNT_TYPE::PHONE)) {
        return false;
    }

    return true;
}