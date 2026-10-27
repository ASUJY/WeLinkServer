#include "json.hpp"
#include "snowflake.h"
#include "userchangemodel.h"
#include "usermodel.h"

struct RegisterRequestInfo {
    std::string phone;
    std::string username;
    std::string password;
    std::string avatar;
};

enum class RegistrationResult {
    SUCCESS = 0,
    INVALID_INPUT,
    PERSISTENCE_FAILURE,
};

class RegistrationService {
    using Json = nlohmann::json;

public:
    RegistrationService(UserModel& userModel, UserChangeModel& UserChangeModel);

    Json Register(const Json& registerInfo);

private:
    bool ValidateRegistrationInfo(const Json& registerInfo, User& registeredUser);
    bool ReadRequiredString(const Json& registerInfo, const std::string& key, size_t maxBytes,
                            std::string& value);

private:
    UserModel& m_userModel;
    UserChangeModel& m_userChangeModel;
    SnowflakeGenerator m_idGenerator;
};