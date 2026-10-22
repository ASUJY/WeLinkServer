#pragma once

#include <string>

struct MySqlConfig {
    std::string host;
    int port;
    std::string user;
    std::string password;
    std::string database;
    std::string charset;
};

struct RedisConfig {
    std::string host;
    int port;
    std::string password;
};

class AppConfig {
public:
    static const AppConfig& Instance();

    MySqlConfig mysql;
    RedisConfig redis;

private:
    AppConfig();
};
