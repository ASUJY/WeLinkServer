#ifndef MYSQLDB_H
#define MYSQLDB_H

#include <mysql/mysql.h>

#include <string>

#include "db.h"

class MySqlDB : public DB {
public:
    MySqlDB();
    ~MySqlDB();

    bool Connect() override;
    bool Update(std::string sql) override;
    MYSQL_RES* Query(std::string sql) override;

private:
    MYSQL* m_conn;
};

#endif