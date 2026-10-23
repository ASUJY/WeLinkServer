#ifndef MYSQLDB_H
#define MYSQLDB_H

#include <mysql/mysql.h>

#include <vector>

#include "db.h"

// 预处理参数类型封装
struct SqlParam {
    enum class Type : int { INT64, STRING };
    Type valType;
    int64_t int64Val;
    std::string strVal;
};

class MySqlDB : public DB {
public:
    MySqlDB();
    ~MySqlDB();

    bool Connect() override;
    bool Update(std::string sql) override;
    MYSQL_RES* Query(std::string sql) override;

    // 预处理接口（杜绝SQL注入） ==========
    bool UpdateStmt(const std::string& sql_template, const std::vector<SqlParam>& params);
    bool QueryStmt(const std::string& sql_template, const std::vector<SqlParam>& params,
                   std::vector<std::vector<std::string>>& rows);

private:
    MYSQL* m_conn;
};

#endif