#include "friendrequestmodel.h"

#include <muduo/base/Logging.h>

#include <iostream>

#include "mysqldb.h"

void FriendRequestModel::Insert(int64_t userId, int64_t friendId) {
    MySqlDB mysql;

    if (!mysql.Connect()) {
        return;
    }

    const std::vector<SqlParam> params = {{SqlParam::Type::INT64, userId, ""},
                                          {SqlParam::Type::INT64, friendId, ""}};
    mysql.UpdateStmt("insert into im_friendrequest(userid, friendid) values(?, ?)", params);
}

void FriendRequestModel::Delete(int64_t userId, int64_t friendId) {
    MySqlDB mysql;

    if (!mysql.Connect()) {
        return;
    }

    const std::vector<SqlParam> params = {{SqlParam::Type::INT64, userId, ""},
                                          {SqlParam::Type::INT64, friendId, ""}};
    mysql.UpdateStmt("DELETE FROM im_friendrequest where userid = ? AND friendid = ?", params);
}

auto FriendRequestModel::Query(int64_t userId) -> std::vector<std::vector<std::string>> {
    MySqlDB mysql;
    std::vector<std::vector<std::string>> rows;
    if (!mysql.Connect()) {
        return rows;
    }

    const std::vector<SqlParam> params = {{SqlParam::Type::INT64, userId, ""},
                                          {SqlParam::Type::INT64, userId, ""}};

    std::string sql = "SELECT * FROM im_friendrequest WHERE userid = ? OR friendid = ?";
    mysql.QueryStmt(sql, params, rows);

    return rows;
}

auto FriendRequestModel::Query(int64_t userId, int64_t friendId)
    -> std::vector<std::vector<std::string>> {
    MySqlDB mysql;
    std::vector<std::vector<std::string>> rows;
    if (!mysql.Connect()) {
        return rows;
    }

    const std::vector<SqlParam> params = {{SqlParam::Type::INT64, userId, ""},
                                          {SqlParam::Type::INT64, friendId, ""}};

    std::string sql = "SELECT * FROM im_friendrequest WHERE userid = ? AND friendid = ?";
    mysql.QueryStmt(sql, params, rows);

    return rows;
}