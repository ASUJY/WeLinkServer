#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

#include "friendmodel.h"
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

void RemoveFriendRelations(int64_t userId) {
    MySqlDB db;
    if (db.Connect()) {
        db.Update("delete from im_friend where userid=" + std::to_string(userId) +
                  " or friendid=" + std::to_string(userId));
    }
}

class UserCleanup {
public:
    explicit UserCleanup(int64_t userId) : m_userId(userId) {}
    ~UserCleanup() { RemoveUser(m_userId); }

private:
    int64_t m_userId;
};

class UserStateCleanup {
public:
    explicit UserStateCleanup(int64_t userId) : m_userId(userId) {}
    ~UserStateCleanup() { RemoveUserState(m_userId); }

private:
    int64_t m_userId;
};

class FriendRelationsCleanup {
public:
    FriendRelationsCleanup(int64_t userA, int64_t userB) : m_userA(userA), m_userB(userB) {}
    ~FriendRelationsCleanup() {
        RemoveFriendRelations(m_userA);
        RemoveFriendRelations(m_userB);
    }

private:
    int64_t m_userA;
    int64_t m_userB;
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

TEST(MySqlIntegrationTest, FriendRelationsAreScopedToTheRequestedUser) {
    RequireIntegrationMySql();
    const int64_t userA   = UniqueId();
    const int64_t userB   = UniqueId();
    const int64_t friendA = UniqueId();
    const int64_t friendB = UniqueId();
    RemoveFriendRelations(userA);
    RemoveFriendRelations(userB);
    FriendRelationsCleanup cleanup(userA, userB);
    FriendModel model;
    model.Insert(userA, friendA);
    model.Insert(userB, friendB);

    MySqlDB db;
    ASSERT_TRUE(db.Connect());
    MYSQL_RES* result =
        db.Query("select count(*) from im_friend where userid=" + std::to_string(userA) +
                 " and friendid=" + std::to_string(friendA));
    ASSERT_NE(result, nullptr);
    MYSQL_ROW row = mysql_fetch_row(result);
    ASSERT_NE(row, nullptr);
    EXPECT_EQ(std::string(row[0]), "1");
    mysql_free_result(result);

    result = db.Query("select count(*) from im_friend where userid=" + std::to_string(userA) +
                      " and friendid=" + std::to_string(friendB));
    ASSERT_NE(result, nullptr);
    row = mysql_fetch_row(result);
    ASSERT_NE(row, nullptr);
    EXPECT_EQ(std::string(row[0]), "0");
    mysql_free_result(result);
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

TEST(MySqlIntegrationTest, UserInsertDoesNotInterpretSqlInjectionPayload) {
    RequireIntegrationMySql();
    const int64_t userId = UniqueId();
    User user(userId);
    const std::string nickname = "welink_' OR '1'='1_" + std::to_string(userId);
    user.SetUserName(nickname);
    user.SetUserPhone("9" + std::to_string(userId).substr(1));
    user.SetPwd("password'); DELETE FROM im_user; --");
    UserCleanup cleanup(userId);

    ASSERT_TRUE(UserModel().Insert(user));
    User found;
    found.SetUserName(nickname);
    ASSERT_TRUE(UserModel().QueryAccount(found, E_ACCOUNT_TYPE::NICKNAME));
    EXPECT_EQ(found.GetUserId(), userId);
    EXPECT_EQ(found.GetUserName(), nickname);
    EXPECT_EQ(found.GetPwd(), "password'); DELETE FROM im_user; --");
}

TEST(MySqlIntegrationTest, UserStateUpdateCannotChangeAnotherUsersRowThroughInjection) {
    RequireIntegrationMySql();
    const int64_t userId      = UniqueId();
    const int64_t otherUserId = UniqueId();
    UserChangeModel model;
    RemoveUserState(userId);
    RemoveUserState(otherUserId);
    UserStateCleanup cleanupUser(userId);
    UserStateCleanup cleanupOther(otherUserId);
    User user(userId);
    User otherUser(otherUserId);
    otherUser.SetUserState("online");
    ASSERT_TRUE(model.Insert(user));
    ASSERT_TRUE(model.Insert(otherUser));

    const std::string injection = "offline' where userid = " + std::to_string(otherUserId) + " -- ";
    EXPECT_TRUE(model.UpdateState(userId, injection));
    EXPECT_EQ(model.QueryState(userId), injection);
    EXPECT_EQ(model.QueryState(otherUserId), "online");
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

TEST(MySqlIntegrationTest, ConcurrentOfflineMessageWritesForOneUserAreComplete) {
    RequireIntegrationMySql();
    constexpr int kWriterCount       = 8;
    constexpr int kMessagesPerWriter = 25;
    const int64_t userId             = UniqueId();
    OfflineMsgModel model;
    model.Remove(userId);
    std::vector<std::thread> writers;
    for (int writer = 0; writer < kWriterCount; ++writer) {
        writers.emplace_back([userId, writer] {
            OfflineMsgModel threadModel;
            for (int message = 0; message < kMessagesPerWriter; ++message) {
                threadModel.Insert(userId, std::to_string(writer) + ":" + std::to_string(message));
            }
        });
    }
    for (auto& writer : writers) writer.join();

    auto messages = model.Query(userId);
    std::sort(messages.begin(), messages.end());
    std::vector<std::string> expected;
    for (int writer = 0; writer < kWriterCount; ++writer) {
        for (int message = 0; message < kMessagesPerWriter; ++message) {
            expected.push_back(std::to_string(writer) + ":" + std::to_string(message));
        }
    }
    std::sort(expected.begin(), expected.end());
    EXPECT_EQ(messages, expected);
    model.Remove(userId);
}

TEST(MySqlIntegrationTest, OfflineMessagePayloadCannotInjectSqlOrChangeOtherUsersRows) {
    RequireIntegrationMySql();
    const int64_t userA = UniqueId();
    const int64_t userB = UniqueId();
    OfflineMsgModel model;
    const std::string payload = "x'); DELETE FROM offlinemessage; --";
    model.Remove(userA);
    model.Remove(userB);
    model.Insert(userA, payload);
    model.Insert(userB, "keep this row");

    EXPECT_EQ(model.Query(userA), (std::vector<std::string>{payload}));
    EXPECT_EQ(model.Query(userB), (std::vector<std::string>{"keep this row"}));
    model.Remove(userA);
    model.Remove(userB);
}