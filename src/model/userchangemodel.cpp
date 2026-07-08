#include "userchangemodel.h"

#include "mysqldb.h"

bool UserChangeModel::Insert(User& user) {
    char sql[1024] = {0};
    sprintf(sql, "insert into im_userchange(userid, state) values(%lld, '%s')", user.GetUserId(),
            user.GetUserState().c_str());

    MySqlDB mysql;
    if (mysql.Connect()) {
        if (mysql.Update(sql)) {
            return true;
        }
    }
    return false;
}

std::string UserChangeModel::QueryState(uint64_t userid) {
    char sql[1024] = {0};
    sprintf(sql, "select state from im_userchange where userid = %ld", userid);

    MySqlDB mysql;
    if (mysql.Connect()) {
        MYSQL_RES* res = mysql.Query(sql);
        if (res != nullptr) {
            MYSQL_ROW row = mysql_fetch_row(res);
            if (row != nullptr) {
                std::string state = row[0];
                mysql_free_result(res);
                return state;
            }
        }
    }

    return "";
}

bool UserChangeModel::UpdateState(User& user) {
    return UpdateState(user.GetUserId(), user.GetUserState());
}

bool UserChangeModel::UpdateState(uint64_t userid, const std::string& state) {
    char sql[1024] = {0};
    sprintf(sql, "update im_userchange set state = '%s' where userid = %ld", state.c_str(), userid);

    MySqlDB mysql;
    if (mysql.Connect()) {
        if (mysql.Update(sql)) {
            return true;
        }
    }
    return false;
}