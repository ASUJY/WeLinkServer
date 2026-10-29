#pragma once

#include <cstdint>
#include <string>
#include <vector>

// 好友申请表
class FriendRequestModel {
public:
    FriendRequestModel()  = default;
    ~FriendRequestModel() = default;

    // 添加好友申请
    void Insert(int64_t userId, int64_t friendId);
    // 删除好友申请
    void Delete(int64_t userId, int64_t friendId);
    // 返回好友申请列表
    auto Query(int64_t userId) -> std::vector<std::vector<std::string>>;
    // 供测试用例使用
    auto Query(int64_t userId, int64_t friendId) -> std::vector<std::vector<std::string>>;
};