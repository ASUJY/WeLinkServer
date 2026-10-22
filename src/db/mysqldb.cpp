#include "mysqldb.h"

#include <muduo/base/Logging.h>

#include "config.h"

MySqlDB::MySqlDB() { m_conn = mysql_init(nullptr); }

MySqlDB::~MySqlDB() {
    // 有没有更好的方法？如何防止数据库句柄泄漏
    if (m_conn != nullptr) {
        mysql_close(m_conn);
        m_conn = nullptr;
    }
}

bool MySqlDB::Connect() {
    const MySqlConfig& config = AppConfig::Instance().mysql;
    MYSQL* p = mysql_real_connect(m_conn, config.host.c_str(), config.user.c_str(),
                                  config.password.c_str(), config.database.c_str(), config.port,
                                  nullptr, 0);

    if (p != nullptr) {
        // 设置字符编码是utf8mb4
        mysql_set_character_set(m_conn, config.charset.c_str());
        LOG_INFO << "connect mysql success!";
    } else {
        LOG_INFO << "connect mysql fail! err: " << mysql_error(m_conn);
    }
    return p;
}

// 更新操作
bool MySqlDB::Update(std::string sql) {
    if (mysql_query(m_conn, sql.c_str())) {
        LOG_INFO << __FILE__ << ":" << __LINE__ << ":" << sql << "更新失败!";
        return false;
    }
    return true;
}

// 查询操作
MYSQL_RES* MySqlDB::Query(std::string sql) {
    if (mysql_query(m_conn, sql.c_str())) {
        LOG_INFO << __FILE__ << ":" << __LINE__ << ":" << sql << "查询失败!";
        return nullptr;
    }
    return mysql_use_result(m_conn);
}

bool MySqlDB::UpdateStmt(const std::string& sql, const std::vector<SqlParam>& params) {
    if (m_conn == nullptr) {
        LOG_ERROR << "mysql connection is null";
        return false;
    }

    MYSQL_STMT* stmt = mysql_stmt_init(m_conn);
    if (!stmt) {
        LOG_ERROR << "mysql_stmt_init fail:" << mysql_error(m_conn);
        return false;
    }

    // 预处理模板
    int ret = mysql_stmt_prepare(stmt, sql.c_str(), sql.size());
    if (ret != 0) {
        LOG_ERROR << "mysql_stmt_prepare fail:" << mysql_stmt_error(stmt);
        mysql_stmt_close(stmt);
        return false;
    }

    unsigned long param_cnt = mysql_stmt_param_count(stmt);
    if (param_cnt != params.size()) {
        LOG_ERROR << "param count mismatch template:" << param_cnt << " input:" << params.size();
        mysql_stmt_close(stmt);
        return false;
    }

    // 分配bing数组
    std::vector<MYSQL_BIND> bind(param_cnt, MYSQL_BIND{});
    // 需要保存字符串指针与长度，避免临时变量销毁
    std::vector<unsigned long> str_len(param_cnt);

    for (size_t i = 0; i < params.size(); ++i) {
        const SqlParam& p = params[i];
        if (p.valType == SqlParam::Type::INT64) {
            bind[i].buffer_type = MYSQL_TYPE_LONGLONG;
            bind[i].buffer      = const_cast<int64_t*>(&p.int64Val);
            bind[i].is_unsigned = false;
        } else if (p.valType == SqlParam::Type::STRING) {
            bind[i].buffer_type   = MYSQL_TYPE_STRING;
            bind[i].buffer        = (void*)p.strVal.data();
            str_len[i]            = p.strVal.size();
            bind[i].buffer_length = str_len[i];
            bind[i].length        = &str_len[i];
        }
    }

    if (mysql_stmt_bind_param(stmt, bind.data()) != 0) {
        LOG_ERROR << "mysql_stmt_bind_param fail:" << mysql_stmt_error(stmt);
        mysql_stmt_close(stmt);
        return false;
    }

    if (mysql_stmt_execute(stmt) != 0) {
        LOG_ERROR << "mysql_stmt_execute fail:" << mysql_stmt_error(stmt);
        mysql_stmt_close(stmt);
        return false;
    }

    mysql_stmt_close(stmt);
    return true;
}