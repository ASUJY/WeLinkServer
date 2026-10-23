#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#include "mysqldb.h"
#include "offlinemessagemodel.h"
#include "userchangemodel.h"
#include "usermodel.h"

namespace {
bool IntegrationEnabled() {
    const char* enabled = std::getenv("WE_LINK_MYSQL_INTEGRATION");
    return enabled != nullptr && std::string(enabled) == "1";
}

void RequireIntegrationMySql() {
    if (!IntegrationEnabled()) {
        GTEST_SKIP() << "Set WE_LINK_MYSQL_INTEGRATION=1 to run MySQL integration tests";
    }
}

int64_t UniqueId() {
    static std::atomic<int64_t> sequence{0};
    return 800000000000000000LL + sequence.fetch_add(1);
}

void RemoveUserState(int64_t userId) {
    MySqlDB db;
    if (db.Connect()) {
        db.Update("delete from im_userchange where userid=" + std::to_string(userId));
    }
}

void RemoveUser(int64_t userId) {
    MySqlDB db;
    if (db.Connect()) {
        db.Update("delete from im_user where uid=" + std::to_string(userId));
    }
}

class UserCleanup {
public:
    explicit UserCleanup(int64_t userId) : m_userId(userId) {}
    ~UserCleanup() { RemoveUser(m_userId); }

private:
    int64_t m_userId;
};
}  // namespace

TEST(MySqlIntegrationTest, OfflineMessagesAreIsolatedAndCanBeRemoved) {
    RequireIntegrationMySql();
    const int64_t userA = UniqueId();
    const int64_t userB = UniqueId();
    OfflineMsgModel model;
    const std::string first  = "message with 'quotes', \\ slash and spaces";
    const std::string second = "second message";

    model.Remove(userA);
    model.Remove(userB);
    model.Insert(userA, first);
    model.Insert(userA, second);
    model.Insert(userB, "other user's message");

    auto messagesA = model.Query(userA);
    auto messagesB = model.Query(userB);
    std::sort(messagesA.begin(), messagesA.end());
    std::sort(messagesB.begin(), messagesB.end());
    auto expectedA = std::vector<std::string>{first, second};
    std::sort(expectedA.begin(), expectedA.end());
    EXPECT_EQ(messagesA, expectedA);
    EXPECT_EQ(messagesB, (std::vector<std::string>{"other user's message"}));

    model.Remove(userA);
    EXPECT_TRUE(model.Query(userA).empty());
    EXPECT_EQ(model.Query(userB), (std::vector<std::string>{"other user's message"}));
    model.Remove(userB);
}

TEST(MySqlIntegrationTest, UserStateCanBeInsertedQueriedAndUpdated) {
    RequireIntegrationMySql();
    const int64_t userId = UniqueId();
    User user(userId);
    UserChangeModel model;
    RemoveUserState(userId);

    ASSERT_TRUE(model.Insert(user));
    EXPECT_EQ(model.QueryState(userId), "offline");
    EXPECT_TRUE(model.UpdateState(userId, "online"));
    EXPECT_EQ(model.QueryState(userId), "online");
    EXPECT_TRUE(model.UpdateState(userId, "offline"));
    EXPECT_EQ(model.QueryState(userId), "offline");

    RemoveUserState(userId);
    EXPECT_TRUE(model.QueryState(userId).empty());
}

TEST(MySqlIntegrationTest, UserCanBeInsertedAndLookedUpByNicknameOrPhone) {
    RequireIntegrationMySql();
    const int64_t userId       = UniqueId();
    const std::string nickname = "welink_test_" + std::to_string(userId);
    const std::string phone    = "9" + std::to_string(userId).substr(1);
    UserModel model;
    User user(userId);
    user.SetUserName(nickname);
    user.SetUserPhone(phone);
    user.SetPwd("test-password");

    RemoveUser(userId);
    UserCleanup cleanup(userId);
    ASSERT_TRUE(model.Insert(user));

    User byNickname;
    byNickname.SetUserName(nickname);
    ASSERT_TRUE(model.QueryAccount(byNickname, E_ACCOUNT_TYPE::NICKNAME));
    EXPECT_EQ(byNickname.GetUserId(), userId);
    EXPECT_EQ(byNickname.GetUserName(), nickname);
    EXPECT_EQ(byNickname.GetUserPhone(), phone);
    EXPECT_EQ(byNickname.GetPwd(), "test-password");

    User byPhone;
    byPhone.SetUserName(phone);
    ASSERT_TRUE(model.QueryAccount(byPhone, E_ACCOUNT_TYPE::PHONE));
    EXPECT_EQ(byPhone.GetUserId(), userId);
    EXPECT_EQ(byPhone.GetUserName(), nickname);

    const auto info = model.QueryUserInfo(nickname, E_ACCOUNT_TYPE::NICKNAME);
    ASSERT_EQ(info.size(), 3u);
    EXPECT_EQ(info[0], std::to_string(userId));
    EXPECT_EQ(info[1], phone);
    EXPECT_EQ(info[2], nickname);

    User missing;
    missing.SetUserName("welink_missing_" + std::to_string(userId));
    EXPECT_FALSE(model.QueryAccount(missing, E_ACCOUNT_TYPE::NICKNAME));
    EXPECT_TRUE(model.QueryUserInfo(missing.GetUserName(), E_ACCOUNT_TYPE::NICKNAME).empty());
}

TEST(MySqlIntegrationTest, AccountLookupTreatsSqlSyntaxAsLiteralInput) {
    RequireIntegrationMySql();

    const int64_t sentinelId = UniqueId();
    User sentinel(sentinelId);
    sentinel.SetUserName("welink_sentinel_" + std::to_string(sentinelId));
    sentinel.SetUserPhone("9" + std::to_string(sentinelId).substr(1));
    sentinel.SetPwd("test-password");
    UserCleanup cleanup(sentinelId);
    ASSERT_TRUE(UserModel().Insert(sentinel));

    User injected;
    // This must be treated as a nickname, not as part of the WHERE clause.
    injected.SetUserName("welink_missing_" + std::to_string(sentinelId) + "' OR '1'='1' -- ");
    EXPECT_FALSE(UserModel().QueryAccount(injected, E_ACCOUNT_TYPE::NICKNAME));
    EXPECT_TRUE(
        UserModel().QueryUserInfo(injected.GetUserName(), E_ACCOUNT_TYPE::NICKNAME).empty());
}

TEST(MySqlIntegrationTest, ConcurrentOfflineMessageWritesRemainCompleteAndIsolated) {
    RequireIntegrationMySql();
    constexpr int kUserCount       = 4;
    constexpr int kMessagesPerUser = 20;
    const int64_t baseId           = UniqueId() + 100000;
    OfflineMsgModel model;
    std::vector<std::thread> writers;

    for (int user = 0; user < kUserCount; ++user) {
        const int64_t userId = baseId + user;
        model.Remove(userId);
        writers.emplace_back([userId, user] {
            OfflineMsgModel threadModel;
            for (int message = 0; message < kMessagesPerUser; ++message) {
                threadModel.Insert(userId, std::to_string(user) + ":" + std::to_string(message));
            }
        });
    }
    for (auto& writer : writers) {
        writer.join();
    }

    for (int user = 0; user < kUserCount; ++user) {
        const int64_t userId = baseId + user;
        auto messages        = model.Query(userId);
        std::sort(messages.begin(), messages.end());
        std::vector<std::string> expected;
        for (int message = 0; message < kMessagesPerUser; ++message) {
            expected.push_back(std::to_string(user) + ":" + std::to_string(message));
        }
        std::sort(expected.begin(), expected.end());
        EXPECT_EQ(messages, expected);
        model.Remove(userId);
    }
}