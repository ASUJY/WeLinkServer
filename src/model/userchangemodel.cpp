#include "userchangemodel.h"

#include "mysqldb.h"

bool UserChangeModel::Insert(User& user) {
    MySqlDB mysql;
    if (!mysql.Connect()) {
        return false;
    }

    const std::vector<SqlParam> params = {
        {SqlParam::Type::INT64, user.GetUserId(), ""},
        {SqlParam::Type::STRING, 0, user.GetUserState()},
    };
    return mysql.UpdateStmt("insert into im_userchange(userid, state) values(?, ?)", params);
}

std::string UserChangeModel::QueryState(int64_t userid) {
    MySqlDB mysql;
    if (!mysql.Connect()) {
        return "";
    }

    std::vector<std::vector<std::string>> rows;
    if (!mysql.QueryStmt("select state from im_userchange where userid = ?",
                         {{SqlParam::Type::INT64, userid, ""}}, rows) ||
        rows.empty() || rows[0].empty()) {
        return "";
    }
    return rows[0][0];
}

bool UserChangeModel::UpdateState(User& user) {
    return UpdateState(user.GetUserId(), user.GetUserState());
}

bool UserChangeModel::UpdateState(int64_t userid, const std::string& state) {
    MySqlDB mysql;
    if (!mysql.Connect()) {
        return false;
    }

    const std::vector<SqlParam> params = {
        {SqlParam::Type::STRING, 0, state},
        {SqlParam::Type::INT64, userid, ""},
    };
    return mysql.UpdateStmt("update im_userchange set state = ? where userid = ?", params);
}