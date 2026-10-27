#include "mysqldb.h"

#include <muduo/base/Logging.h>

#include <memory>

#include "config.h"

namespace {
class MySqlClientLibrary {
public:
    MySqlClientLibrary() : m_initialized(mysql_library_init(0, nullptr, nullptr) == 0) {}

    ~MySqlClientLibrary() {
        if (m_initialized) mysql_library_end();
    }

    bool Initialized() const { return m_initialized; }

private:
    bool m_initialized;
};

// Initialize before main starts application threads. Service is initialized later, so its
// worker pool is destroyed before this library cleanup runs at process shutdown.
MySqlClientLibrary g_mysqlClientLibrary;

class MySqlThreadCleanup {
public:
    ~MySqlThreadCleanup() {
        if (m_used) mysql_thread_end();
    }

    void MarkUsed() { m_used = true; }

private:
    bool m_used = false;
};

MySqlThreadCleanup& CurrentThreadCleanup() {
    static thread_local MySqlThreadCleanup cleanup;
    return cleanup;
}
};  // namespace

MySqlDB::MySqlDB() : m_conn(nullptr) {
    if (!g_mysqlClientLibrary.Initialized()) return;

    // mysql_init initializes the calling thread's MySQL state. Release it when that thread exits.
    CurrentThreadCleanup().MarkUsed();

    m_conn = mysql_init(nullptr);
}

MySqlDB::~MySqlDB() {
    // 有没有更好的方法？如何防止数据库句柄泄漏
    if (m_conn != nullptr) {
        mysql_close(m_conn);
        m_conn = nullptr;
    }
}

bool MySqlDB::Connect() {
    if (m_conn == nullptr) {
        LOG_ERROR << "mysql_init failed";
        return false;
    }

    const MySqlConfig& config = AppConfig::Instance().mysql;
    MYSQL* p = mysql_real_connect(m_conn, config.host.c_str(), config.user.c_str(),
                                  config.password.c_str(), config.database.c_str(), config.port,
                                  nullptr, 0);

    if (p != nullptr) {
        // 设置字符编码是utf8mb4
        mysql_set_character_set(m_conn, config.charset.c_str());
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

bool MySqlDB::QueryStmt(const std::string& sql, const std::vector<SqlParam>& params,
                        std::vector<std::vector<std::string>>& rows) {
    rows.clear();
    if (m_conn == nullptr) {
        LOG_ERROR << "mysql connection is null";
        return false;
    }

    MYSQL_STMT* stmt = mysql_stmt_init(m_conn);
    if (!stmt) {
        LOG_ERROR << "mysql_stmt_init fail:" << mysql_error(m_conn);
        return false;
    }

    auto closeStmt = [&stmt]() {
        if (stmt != nullptr) {
            mysql_stmt_close(stmt);
            stmt = nullptr;
        }
    };

    if (mysql_stmt_prepare(stmt, sql.c_str(), static_cast<unsigned long>(sql.size())) != 0) {
        LOG_ERROR << "mysql_stmt_prepare fail:" << mysql_stmt_error(stmt);
        closeStmt();
        return false;
    }
    if (mysql_stmt_param_count(stmt) != params.size()) {
        LOG_ERROR << "mysql statement parameter count mismatch";
        closeStmt();
        return false;
    }

    std::vector<MYSQL_BIND> paramBinds(params.size(), MYSQL_BIND{});
    std::vector<unsigned long> paramLengths(params.size(), 0);
    for (size_t i = 0; i < params.size(); ++i) {
        if (params[i].valType == SqlParam::Type::INT64) {
            paramBinds[i].buffer_type = MYSQL_TYPE_LONGLONG;
            paramBinds[i].buffer      = const_cast<int64_t*>(&params[i].int64Val);
            paramBinds[i].is_unsigned = false;
        } else {
            paramBinds[i].buffer_type   = MYSQL_TYPE_STRING;
            paramBinds[i].buffer        = const_cast<char*>(params[i].strVal.data());
            paramLengths[i]             = static_cast<unsigned long>(params[i].strVal.size());
            paramBinds[i].buffer_length = paramLengths[i];
            paramBinds[i].length        = &paramLengths[i];
        }
    }
    if (!paramBinds.empty() && mysql_stmt_bind_param(stmt, paramBinds.data()) != 0) {
        LOG_ERROR << "mysql_stmt_bind_param fail:" << mysql_stmt_error(stmt);
        closeStmt();
        return false;
    }
    if (mysql_stmt_execute(stmt) != 0) {
        LOG_ERROR << "mysql_stmt_execute fail:" << mysql_stmt_error(stmt);
        closeStmt();
        return false;
    }

    MYSQL_RES* metadata = mysql_stmt_result_metadata(stmt);
    if (metadata == nullptr) {
        LOG_ERROR << "mysql statement did not return result metadata";
        closeStmt();
        return false;
    }
    const unsigned int columnCount = mysql_num_fields(metadata);
    if (mysql_stmt_store_result(stmt) != 0) {
        LOG_ERROR << "mysql_stmt_store_result fail:" << mysql_stmt_error(stmt);
        mysql_free_result(metadata);
        closeStmt();
        return false;
    }

    std::vector<std::vector<char>> buffers(columnCount, std::vector<char>(1, '\0'));
    std::vector<unsigned long> lengths(columnCount, 0);
    std::unique_ptr<bool[]> nullFlags(new bool[columnCount]{});
    std::vector<MYSQL_BIND> resultBinds(columnCount, MYSQL_BIND{});
    for (unsigned int i = 0; i < columnCount; ++i) {
        resultBinds[i].buffer_type   = MYSQL_TYPE_STRING;
        resultBinds[i].buffer        = buffers[i].data();
        resultBinds[i].buffer_length = 1;
        resultBinds[i].length        = &lengths[i];
        resultBinds[i].is_null       = &nullFlags[i];
    }
    if (columnCount > 0 && mysql_stmt_bind_result(stmt, resultBinds.data()) != 0) {
        LOG_ERROR << "mysql_stmt_bind_result fail:" << mysql_stmt_error(stmt);
        mysql_free_result(metadata);
        closeStmt();
        return false;
    }

    int fetchStatus = 0;
    while ((fetchStatus = mysql_stmt_fetch(stmt)) == 0 || fetchStatus == MYSQL_DATA_TRUNCATED) {
        std::vector<std::string> row(columnCount);
        for (unsigned int i = 0; i < columnCount; ++i) {
            if (nullFlags[i] || lengths[i] == 0) {
                continue;
            }
            row[i].resize(lengths[i]);
            MYSQL_BIND column{};
            column.buffer_type   = MYSQL_TYPE_STRING;
            column.buffer        = &row[i][0];
            column.buffer_length = lengths[i];
            column.length        = &lengths[i];
            if (mysql_stmt_fetch_column(stmt, &column, i, 0) != 0) {
                LOG_ERROR << "mysql_stmt_fetch_column fail:" << mysql_stmt_error(stmt);
                mysql_free_result(metadata);
                closeStmt();
                rows.clear();
                return false;
            }
        }
        rows.emplace_back(std::move(row));
    }
    const bool ok = fetchStatus == MYSQL_NO_DATA;
    if (!ok) {
        LOG_ERROR << "mysql_stmt_fetch fail:" << mysql_stmt_error(stmt);
        rows.clear();
    }
    mysql_free_result(metadata);
    closeStmt();
    return ok;
}