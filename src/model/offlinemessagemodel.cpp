#include "offlinemessagemodel.h"

#include "mysqldb.h"

// 存储用户的离线消息
void OfflineMsgModel::Insert(int64_t userid, string msg) {
    MySqlDB mysql;
    if (!mysql.Connect()) {
        return;
    }

    // SQL模板，使用 ? 占位符，禁止拼接任何用户数据
    std::string sql = "insert into offlinemessage values(?, ?)";
    std::vector<SqlParam> params;
    params.push_back({SqlParam::Type::INT64, userid, ""});
    params.push_back({SqlParam::Type::STRING, 0, msg});

    bool ok = mysql.UpdateStmt(sql, params);
}

// 删除用户的离线消息
void OfflineMsgModel::Remove(int64_t userid) {
    // 组装sql语句
    char sql[1024] = {0};
    sprintf(sql, "delete from offlinemessage where userid=%ld", userid);

    MySqlDB mysql;
    if (mysql.Connect()) {
        mysql.Update(sql);
    }
}

// 查询用户的离线消息
vector<string> OfflineMsgModel::Query(int64_t userid) {
    // 组装sql语句
    char sql[1024] = {0};
    sprintf(sql, "select message from offlinemessage where userid=%ld", userid);

    vector<string> vec;
    MySqlDB mysql;
    if (mysql.Connect()) {
        MYSQL_RES* res = mysql.Query(sql);
        if (res != nullptr) {
            // 把userid用户的所有离线消息放入vec中返回
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(res)) != nullptr) {
                vec.push_back(row[0]);
            }
            mysql_free_result(res);
            return vec;
        }
    }
    return vec;
}
