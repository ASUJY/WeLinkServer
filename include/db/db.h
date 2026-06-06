#ifndef DB_H
#define DB_H

#include <string>

class DB {
public:
    DB() = default;
    virtual ~DB() = default;

    virtual bool Connect() = 0;
    virtual bool Update(std::string sql) = 0;

};

#endif