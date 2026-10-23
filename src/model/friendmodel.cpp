#include "friendmodel.h"

#include "mysqldb.h"

void FriendModel::Insert(int64_t userid, int64_t friendid) {
    MySqlDB mysql;
    if (!mysql.Connect()) {
        return;
    }

    const std::vector<SqlParam> params = {
        {SqlParam::Type::INT64, userid, ""},
        {SqlParam::Type::INT64, friendid, ""},
    };
    mysql.UpdateStmt("insert into im_friend(userid, friendid) values(?, ?)", params);
}

// 返回用户好友列表
vector<User> FriendModel::Query(int64_t userid) {
    vector<User> users;
    MySqlDB mysql;
    if (!mysql.Connect()) {
        return users;
    }

    std::vector<std::vector<std::string>> rows;
    const std::vector<SqlParam> params = {{SqlParam::Type::INT64, userid, ""}};
    if (!mysql.QueryStmt("select a.id, a.name, a.state from im_user a inner join im_friend b on "
                         "b.friendid = a.id where b.userid = ?",
                         params, rows)) {
        return users;
    }

    for (const auto& row : rows) {
        if (row.size() < 3) {
            continue;
        }
        try {
            User user;
            user.SetUserId(std::stoll(row[0]));
            user.SetUserName(row[1]);
            user.SetUserState(row[2]);
            users.push_back(user);
        } catch (const std::exception&) {
            // Ignore malformed database rows rather than terminating the request thread.
        }
    }

    return users;
}