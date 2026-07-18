#include "friendmodel.h"

#include "mysqldb.h"

// 添加好友关系
void FriendModel::Insert(int64_t userid, int64_t friendid) {
    // 组装sql语句
    char sql[1024] = {0};
    sprintf(sql, "insert into im_friend values(%d, %d)", userid, friendid);

    MySqlDB mysql;
    if (mysql.Connect()) {
        mysql.Update(sql);
    }
}

// 返回用户好友列表
vector<User> FriendModel::Query(int64_t userid) {
    // 组装sql语句
    char sql[1024] = {0};
    sprintf(sql,
            "select a.id,a.name,a.state from im_user a inner join im_friend b on "
            "b.friendid = a.id where b.userid = %d",
            userid);

    vector<User> vec;
    MySqlDB mysql;
    if (mysql.Connect()) {
        MYSQL_RES* res = mysql.Query(sql);
        if (res != nullptr) {
            // 把userid用户的所有离线消息放入vec中返回
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(res)) != nullptr) {
                User user;
                user.SetUserId(atoi(row[0]));
                user.SetUserName(row[1]);
                user.SetUserState(row[2]);
                vec.push_back(user);
            }
            mysql_free_result(res);
            return vec;
        }
    }
    return vec;
}