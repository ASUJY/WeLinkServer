#include "registrationservice.h"

#include <muduo/base/Logging.h>

#include "common.h"

RegistrationService::RegistrationService(UserModel& userModel, UserChangeModel& userChangeModel)
    : m_userModel(userModel), m_userChangeModel(userChangeModel), m_idGenerator(1, 1) {}

RegistrationService::Json RegistrationService::Register(Json& registerInfo) {
    nlohmann::json response;
    User registeredUser;
    if (!ValidateRegistrationInfo(registerInfo, registeredUser)) {
        response["msgtype"]         = E_MSG_TYPE::REG_MSG_ACK;
        response["data"]["errtype"] = E_ERR_TYPE::REG_MSG_ACK_INVALID_INPUT;
        return response;
    }

    if (!m_userModel.Insert(registeredUser) || !m_userChangeModel.Insert(registeredUser)) {
        response["msgtype"]         = E_MSG_TYPE::REG_MSG_ACK;
        response["data"]["errtype"] = E_ERR_TYPE::USER_EXIT;
        return response;
    }

    LOG_INFO << __FILE__ << ":" << __LINE__ << "register success!!!!";
    response["msgtype"]          = E_MSG_TYPE::REG_MSG_ACK;
    response["data"]["errtype"]  = E_ERR_TYPE::REG_MSG_ACK_SUCCESS;
    response["data"]["userid"]   = registeredUser.GetUserId();
    response["data"]["username"] = registeredUser.GetUserName();
    response["data"]["phone"]    = registeredUser.GetUserPhone();
    response["data"]["avator"]   = registeredUser.GetUserAvatar();
    return response;
}

bool RegistrationService::ValidateRegistrationInfo(const Json& registerInfo, User& registeredUser) {
    if (!registerInfo.is_object() || !registerInfo.contains("data") ||
        !registerInfo.at("data").is_object()) {
        return false;
    }

    const auto& data = registerInfo.at("data");
    RegisterRequestInfo result;
    if (!ReadRequiredString(registerInfo, "phone", 32, result.phone) ||
        !ReadRequiredString(data, "username", 64, result.username) ||
        !ReadRequiredString(data, "password", 128, result.password)) {
        return false;
    }

    // 头像可选：缺失时使用空字符串；
    result.avatar.clear();
    if (data.contains("avatar")) {
        if (!data.at("avatar").is_string() ||
            data.at("avatar").get_ref<const std::string&>().size() > 512) {
            return false;
        }
        result.avatar = data.at("avatar").get<std::string>();
    }
    registeredUser.SetUserId(m_idGenerator.NextId());
    registeredUser.SetUserPhone(result.phone);
    registeredUser.SetUserName(result.username);
    registeredUser.SetPwd(result.password);
    registeredUser.SetUserAvatar(result.avatar);

    return true;
}

bool RegistrationService::ReadRequiredString(const Json& registerInfo, const std::string& key,
                                             size_t maxBytes, std::string& value) {
    if (!registerInfo.is_object() || !registerInfo.contains(key) ||
        !registerInfo.at(key).is_string()) {
        return false;
    }

    const auto& field = registerInfo.at(key).get_ref<const std::string&>();
    if (field.empty() || field.size() > maxBytes) {
        return false;
    }

    value = field;
    return true;
}