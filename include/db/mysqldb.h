#ifndef MYSQLDB_H
#define MYSQLDB_H

#include "db.h"
#include <mysql/mysql.h>
#include <string>

class MySqlDB : public DB {

public:
    MySqlDB();
    ~MySqlDB();

    bool Connect() override;
    bool Update(std::string sql) override;

private:
    MYSQL *m_conn;
};

#endif