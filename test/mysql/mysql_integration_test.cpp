#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <string>
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