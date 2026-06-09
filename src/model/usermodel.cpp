#include "usermodel.h"
#include "mysqldb.h"
#include <iostream>

bool UserModel::Insert(User &user) {
    char sql[1024] = {0};
    sprintf(
        sql, "insert into im_user(uid, nickname, password, phone) values(%d, '%s', '%s', '%s')",
        1, user.GetUserName().c_str(), user.GetPwd().c_str(), user.GetUserPhone().c_str()
    );

    MySqlDB mysql;
    if (mysql.Connect()) {
        if (mysql.Update(sql)) {
            // user.SetUserId()
            return true;
        }
    }
    return false;
}

bool UserModel::Query(User &user) {
    // 后续可以根据用户的账号，手机号，用户名进行登录
    // 组装sql语句
    char sql[1024] = {0};
    sprintf(sql, "select * from im_user where nickname = '%s'", user.GetUserName().c_str());

    MySqlDB mysql;
    if (mysql.Connect()) {
        MYSQL_RES *res = mysql.Query(sql);
        if (res != nullptr) {
            // 获取一行结果
            MYSQL_ROW row = mysql_fetch_row(res);
            if (row != nullptr) {
                std::string passwd = row[3];
                mysql_free_result(res);
                if (passwd == user.GetPwd()) {
                    return true;
                }
            }
        }
    }
    return false;
}

std::vector<std::string> UserModel::QueryUserInfo(std::string username) {
    char sql[1024] = {0};
    sprintf(sql, "select * from im_user where nickname = '%s'", username.c_str());
    std::cout << "sql: " << sql << std::endl;
    std::vector<std::string> resVec;
    MySqlDB mysql;
    if (mysql.Connect()) {
        MYSQL_RES *res = mysql.Query(sql);
        if (res != nullptr) {
            // 获取一行结果
            MYSQL_ROW row = mysql_fetch_row(res);
            if (row != nullptr) {
                resVec.emplace_back(row[2]);
                resVec.emplace_back(row[3]);
                mysql_free_result(res);
            }
        }
    }
    return resVec;
}