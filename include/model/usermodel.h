#ifndef USERMODEl_H
#define USERMODEl_H

#include <vector>

#include "common.h"
#include "user.hpp"

// User表的数据操作类
class UserModel {
public:
    // User表的增加方法
    bool Insert(User& user);

    // 查询用户信息
    bool QueryAccount(User& user, E_ACCOUNT_TYPE type);

    std::vector<std::string> QueryUserInfo(std::string username, E_ACCOUNT_TYPE type);
};

#endif