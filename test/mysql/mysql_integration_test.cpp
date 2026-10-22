#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <string>
#include <vector>

#include "mysqldb.h"
#include "offlinemessagemodel.h"
#include "userchangemodel.h"

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