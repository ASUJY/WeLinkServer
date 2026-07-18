#ifndef FRIENDMODEL_H
#define FRIENDMODEL_H

#include <vector>

#include "user.hpp"
using namespace std;

// 维护好友信息的操作接口方法
class FriendModel {
public:
    // 添加好友关系
    void Insert(int64_t userid, int64_t friendid);

    // 返回用户好友列表
    vector<User> Query(int64_t userid);
};

#endif