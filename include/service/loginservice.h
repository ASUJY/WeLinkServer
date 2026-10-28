#pragma once

#include "json.hpp"
#include "offlinemessagemodel.h"
#include "userchangemodel.h"
#include "usermodel.h"

class LoginService {
    using Json = nlohmann::json;

public:
    using SessionRegistrar = std::function<bool(User&)>;

    LoginService(UserModel& userModel, UserChangeModel& UserChangeModel,
                 OfflineMsgModel& offlineMsgModel);
    Json Login(const Json& loginInfo, const SessionRegistrar& registerSession);

private:
    bool ValidateLoginInfo(const Json& loginInfo, int& accountTypeValue);
    bool ReadRequiredString(const Json& loginInfo, const std::string& key, size_t maxBytes);
    bool ValidateAccountType(const Json& loginInfo, int& accountTypeValue);

private:
    UserModel& m_userModel;
    UserChangeModel& m_userChangeModel;
    OfflineMsgModel& m_offlineMsgModel;
};