#include "mysqldb.h"
#include <string>
#include <muduo/base/Logging.h>

using namespace std;

// 数据库配置信息
static string server = "127.0.0.1";
static string user = "root";
static string password = "root";
static string dbname = "im_db";

MySqlDB::MySqlDB() {
    m_conn = mysql_init(nullptr);
}

MySqlDB::~MySqlDB() {
    // 有没有更好的方法？如何防止数据库句柄泄漏
    if (m_conn != nullptr) {
        mysql_close(m_conn);
    }
}

bool MySqlDB::Connect() {
    MYSQL *p = mysql_real_connect(m_conn, server.c_str(), user.c_str(), password.c_str(), dbname.c_str(), 3306, nullptr, 0);

    if (p != nullptr) {
        // C和C++代码默认的编码字符是ASCII，如果不设置，从MySQL上拉下来的中文显示会乱码
        mysql_query(m_conn, "set names gbk");
        LOG_INFO << "connect mysql success!";
    } else {
        LOG_INFO << "connect mysql fail!";
    }
    return p;
}

// 更新操作
bool MySqlDB::Update(string sql) {
    if (mysql_query(m_conn, sql.c_str())) {
        LOG_INFO << __FILE__ << ":" << __LINE__ << ":" << sql << "更新失败!";
        return false;
    }
    return true;
}

// 查询操作
MYSQL_RES* MySqlDB::Query(string sql) {
    if (mysql_query(m_conn, sql.c_str())) {
        LOG_INFO << __FILE__ << ":" << __LINE__ << ":" << sql << "查询失败!";
        return nullptr;
    }
    return mysql_use_result(m_conn);
}