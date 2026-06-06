#include "usermodel.h"
#include "mysqldb.h"

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