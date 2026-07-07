#ifndef OFFLINEMESSAGEMODEL_H
#define OFFLINEMESSAGEMODEL_H

#include <string>
#include <vector>
using namespace std;

// 提供离线消息表的操作接口方法
class OfflineMsgModel {
public:
    // 存储用户的离线消息
    void Insert(uint64_t userid, string msg);
    // 删除用户的离线消息
    void Remove(uint64_t userid);
    // 查询用户的离线消息
    vector<string> Query(uint64_t userid);
};

#endif