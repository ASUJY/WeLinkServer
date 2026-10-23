#include "config.h"

#include <fstream>

#include "json.hpp"

namespace {
int ReadPort(const nlohmann::json& value, const std::string& name) {
    if (!value.is_number_integer()) {
        throw std::runtime_error("configuration field '" + name + "' must be an integer");
    }
    const int port = value.get<int>();
    if (port < 1 || port > 65535) {
        throw std::runtime_error("configuration field '" + name + "' must be between 1 and 65535");
    }
    return port;
}

std::string ConfigPath() {
    const char* path = std::getenv("WE_LINK_CONFIG");
    return path && *path ? path : "config.json";
}
}  // namespace

AppConfig::AppConfig() : AppConfig(ConfigPath()) {}

AppConfig::AppConfig(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        throw std::runtime_error("cannot open configuration file '" + path +
                                 "' (copy config.example.json to config.json)");
    }

    nlohmann::json root;

    try {
        file >> root;
        const auto& mysqlJson = root.at("mysql");
        const auto& redisJson = root.at("redis");

        mysql.host     = mysqlJson.at("host").get<std::string>();
        mysql.port     = ReadPort(mysqlJson.at("port"), "mysql.port");
        mysql.user     = mysqlJson.at("user").get<std::string>();
        mysql.password = mysqlJson.at("password").get<std::string>();
        mysql.database = mysqlJson.at("database").get<std::string>();
        mysql.charset  = mysqlJson.at("charset").get<std::string>();

        redis.host     = redisJson.at("host").get<std::string>();
        redis.port     = ReadPort(redisJson.at("port"), "redis.port");
        redis.password = redisJson.value("password", std::string());
    } catch (const std::exception& error) {
        throw std::runtime_error("invalid configuration file '" + path + "': " + error.what());
    }
}

const AppConfig& AppConfig::Instance() {
    static const AppConfig config;
    return config;
}

AppConfig AppConfig::LoadFromFile(const std::string& path) { return AppConfig(path); }