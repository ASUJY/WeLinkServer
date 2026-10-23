#include "offlinemessagemodel.h"

#include "mysqldb.h"

void OfflineMsgModel::Insert(int64_t userid, string msg) {
    MySqlDB mysql;
    if (!mysql.Connect()) {
        return;
    }

    const std::vector<SqlParam> params = {
        {SqlParam::Type::INT64, userid, ""},
        {SqlParam::Type::STRING, 0, msg},
    };

    mysql.UpdateStmt("insert into offlinemessage(userid, message) values(?, ?)", params);
}

// 删除用户的离线消息
void OfflineMsgModel::Remove(int64_t userid) {
    MySqlDB mysql;
    if (!mysql.Connect()) {
        return;
    }

    mysql.UpdateStmt("delete from offlinemessage where userid = ?",
                     {{SqlParam::Type::INT64, userid, ""}});
}

// 查询用户的离线消息
vector<string> OfflineMsgModel::Query(int64_t userid) {
    vector<string> messages;
    MySqlDB mysql;
    if (!mysql.Connect()) {
        return messages;
    }

    std::vector<std::vector<std::string>> rows;
    if (!mysql.QueryStmt("select message from offlinemessage where userid = ?",
                         {{SqlParam::Type::INT64, userid, ""}}, rows)) {
        return messages;
    }
    for (const auto& row : rows) {
        if (!row.empty()) {
            messages.push_back(row[0]);
        }
    }
    return messages;
}
