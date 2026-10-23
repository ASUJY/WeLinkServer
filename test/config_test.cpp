#include "config.h"

#include <gtest/gtest.h>

#include <atomic>
#include <fstream>

namespace {
class ConfigFile {
public:
    ConfigFile() {
        static std::atomic<unsigned> sequence{0};
        m_path = "/tmp/welink_config_test_" + std::to_string(getpid()) + "_" +
                 std::to_string(sequence.fetch_add(1)) + ".json";
    }

    ~ConfigFile() { std::remove(m_path.c_str()); }

    const std::string& Path() const { return m_path; }

    void Write(const std::string& contents) const {
        std::ofstream file(m_path);
        ASSERT_TRUE(file.is_open());
        file << contents;
        ASSERT_TRUE(file.good());
    }

private:
    std::string m_path;
};

const char* kValidConfig = R"({
  "mysql": {
    "host": "db.example", "port": 3306, "user": "welink",
    "password": "secret", "database": "im_db", "charset": "utf8mb4"
  },
  "redis": {"host": "cache.example", "port": 6379}
})";
}  // namespace

TEST(ConfigTest, LoadsRequiredFieldsAndDefaultsRedisPassword) {
    ConfigFile file;
    file.Write(kValidConfig);

    const AppConfig config = AppConfig::LoadFromFile(file.Path());
    EXPECT_EQ(config.mysql.host, "db.example");
    EXPECT_EQ(config.mysql.port, 3306);
    EXPECT_EQ(config.mysql.user, "welink");
    EXPECT_EQ(config.mysql.database, "im_db");
    EXPECT_EQ(config.mysql.charset, "utf8mb4");
    EXPECT_EQ(config.redis.host, "cache.example");
    EXPECT_EQ(config.redis.port, 6379);
    EXPECT_TRUE(config.redis.password.empty());
}

TEST(ConfigTest, RejectsMissingFile) {
    EXPECT_THROW(AppConfig::LoadFromFile("/tmp/welink_config_file_does_not_exist.json"),
                 std::runtime_error);
}

TEST(ConfigTest, RejectsMalformedJsonAndMissingRequiredFields) {
    ConfigFile file;
    file.Write("{");
    EXPECT_THROW(AppConfig::LoadFromFile(file.Path()), std::runtime_error);

    file.Write(R"({"mysql":{},"redis":{}})");
    EXPECT_THROW(AppConfig::LoadFromFile(file.Path()), std::runtime_error);
}

TEST(ConfigTest, RejectsInvalidPortTypesAndRanges) {
    ConfigFile file;
    file.Write(
        R"({"mysql":{"host":"h","port":"3306","user":"u","password":"p","database":"d","charset":"utf8mb4"},"redis":{"host":"r","port":6379}})");
    EXPECT_THROW(AppConfig::LoadFromFile(file.Path()), std::runtime_error);

    file.Write(
        R"({"mysql":{"host":"h","port":0,"user":"u","password":"p","database":"d","charset":"utf8mb4"},"redis":{"host":"r","port":6379}})");
    EXPECT_THROW(AppConfig::LoadFromFile(file.Path()), std::runtime_error);

    file.Write(
        R"({"mysql":{"host":"h","port":65536,"user":"u","password":"p","database":"d","charset":"utf8mb4"},"redis":{"host":"r","port":6379}})");
    EXPECT_THROW(AppConfig::LoadFromFile(file.Path()), std::runtime_error);

    file.Write(
        R"({"mysql":{"host":"h","port":3306,"user":"u","password":"p","database":"d","charset":"utf8mb4"},"redis":{"host":"r","port":-1}})");
    EXPECT_THROW(AppConfig::LoadFromFile(file.Path()), std::runtime_error);
}