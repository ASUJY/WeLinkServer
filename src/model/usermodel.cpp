#include "usermodel.h"

#include <iostream>

#include "mysqldb.h"

bool UserModel::Insert(User& user) {
    char sql[1024] = {0};
    sprintf(sql,
            "insert into im_user(uid, nickname, password, phone) values(%ld, '%s', '%s', '%s')",
            user.GetUserId(), user.GetUserName().c_str(), user.GetPwd().c_str(),
            user.GetUserPhone().c_str());

    MySqlDB mysql;
    if (mysql.Connect()) {
        if (mysql.Update(sql)) {
            // user.SetUserId()
            return true;
        }
    }
    return false;
}

bool UserModel::QueryAccount(User& user, E_ACCOUNT_TYPE type) {
    // 后续可以根据用户的账号，手机号，用户名进行登录
    // 组装sql语句
    char sql[1024] = {0};
    if (type == E_ACCOUNT_TYPE::NICKNAME) {
        sprintf(sql, "select * from im_user where nickname = '%s'", user.GetUserName().c_str());
        std::cout << "QueryAccount: 用户名" << std::endl;
    } else if (type == E_ACCOUNT_TYPE::PHONE) {
        sprintf(sql, "select * from im_user where phone = '%s'", user.GetUserName().c_str());
        std::cout << "QueryAccount: 手机号" << std::endl;
    }

    MySqlDB mysql;
    if (mysql.Connect()) {
        MYSQL_RES* res = mysql.Query(sql);
        if (res != nullptr) {
            // 获取一行结果
            MYSQL_ROW row = mysql_fetch_row(res);
            if (row != nullptr) {
                user.SetPwd(row[5]);
                // if (passwd == user.GetPwd()) {
                user.SetUserName(row[3]);
                user.SetUserId(std::stoll(row[0]));
                user.SetUserPhone(row[2]);
                mysql_free_result(res);
                return true;
                // }
            }
        }
        mysql_free_result(res);
    }
    return false;
}

std::vector<std::string> UserModel::QueryUserInfo(std::string username, E_ACCOUNT_TYPE type) {
    char sql[1024] = {0};

    if (type == E_ACCOUNT_TYPE::NICKNAME) {
        sprintf(sql, "select * from im_user where nickname = '%s'", username.c_str());
        std::cout << "QueryAccount: 用户名" << std::endl;
    } else if (type == E_ACCOUNT_TYPE::PHONE) {
        sprintf(sql, "select * from im_user where phone = '%s'", username.c_str());
        std::cout << "QueryAccount: 手机号" << std::endl;
    }

    std::cout << "sql: " << sql << std::endl;
    std::vector<std::string> resVec;
    MySqlDB mysql;
    if (mysql.Connect()) {
        MYSQL_RES* res = mysql.Query(sql);
        if (res != nullptr) {
            // 获取一行结果
            MYSQL_ROW row = mysql_fetch_row(res);
            if (row != nullptr) {
                resVec.emplace_back(row[0]);
                resVec.emplace_back(row[2]);
                resVec.emplace_back(row[3]);
                mysql_free_result(res);
            }
        }
    }
    return resVec;
}