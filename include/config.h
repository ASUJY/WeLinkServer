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
    // Parse an explicit file without changing the process-wide singleton.
    // For Unit Test
    static AppConfig LoadFromFile(const std::string& path);

    MySqlConfig mysql;
    RedisConfig redis;

private:
    AppConfig();
    explicit AppConfig(const std::string& path);
};
