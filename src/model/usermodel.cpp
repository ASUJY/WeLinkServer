#include "usermodel.h"

#include "mysqldb.h"

namespace {
bool BuildAccountQuery(E_ACCOUNT_TYPE type, std::string& sql) {
    if (type == E_ACCOUNT_TYPE::NICKNAME) {
        sql = "select uid, phone, nickname, password from im_user where nickname = ?";
        return true;
    }
    if (type == E_ACCOUNT_TYPE::PHONE) {
        sql = "select uid, phone, nickname, password from im_user where phone = ?";
        return true;
    }
    return false;
}

bool BuildUserInfoQuery(E_ACCOUNT_TYPE type, std::string& sql) {
    if (type == E_ACCOUNT_TYPE::NICKNAME) {
        sql = "select uid, phone, nickname from im_user where nickname = ?";
        return true;
    }
    if (type == E_ACCOUNT_TYPE::PHONE) {
        sql = "select uid, phone, nickname from im_user where phone = ?";
        return true;
    }
    return false;
}

SqlParam StringParam(const std::string& value) { return {SqlParam::Type::STRING, 0, value}; }
}  // namespace

bool UserModel::Insert(User& user) {
    MySqlDB mysql;
    if (!mysql.Connect()) {
        return false;
    }

    const std::vector<SqlParam> params = {
        {SqlParam::Type::INT64, user.GetUserId(), ""},
        StringParam(user.GetUserName()),
        StringParam(user.GetPwd()),
        StringParam(user.GetUserPhone()),
    };

    return mysql.UpdateStmt(
        "insert into im_user(uid, nickname, password, phone) values(?, ?, ?, ?)", params);
}

bool UserModel::QueryAccount(User& user, E_ACCOUNT_TYPE type) {
    std::string sql;
    if (!BuildAccountQuery(type, sql)) {
        return false;
    }

    MySqlDB mysql;
    if (!mysql.Connect()) {
        return false;
    }

    std::vector<std::vector<std::string>> rows;
    if (!mysql.QueryStmt(sql, {StringParam(user.GetUserName())}, rows) || rows.empty() ||
        rows[0].size() < 4) {
        return false;
    }

    try {
        user.SetUserId(std::stoll(rows[0][0]));
    } catch (const std::exception&) {
        return false;
    }
    user.SetUserPhone(rows[0][1]);
    user.SetUserName(rows[0][2]);
    user.SetPwd(rows[0][3]);
    return true;
}

std::vector<std::string> UserModel::QueryUserInfo(const std::string username, E_ACCOUNT_TYPE type) {
    std::string sql;
    if (!BuildUserInfoQuery(type, sql)) {
        return {};
    }

    MySqlDB mysql;
    if (!mysql.Connect()) {
        return {};
    }

    std::vector<std::vector<std::string>> rows;
    if (!mysql.QueryStmt(sql, {StringParam(username)}, rows) || rows.empty() ||
        rows[0].size() < 3) {
        return {};
    }
    return {rows[0][0], rows[0][1], rows[0][2]};
}