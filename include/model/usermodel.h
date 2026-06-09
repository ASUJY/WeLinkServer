#ifndef USERMODEl_H
#define USERMODEl_H

#include "user.hpp"
#include <vector>

// User表的数据操作类
class UserModel {
public:
    // User表的增加方法
    bool Insert(User &user);

    // 查询用户信息
    bool Query(User &user);

    std::vector<std::string> QueryUserInfo(std::string username);
private:

};

#endif