set (TEST_SRC snowflake_test.cpp)

add_executable(run_all_test ${TEST_SRC})
target_link_libraries(run_all_test
    chatserver_lib
    GTest::gtest_main
    GTest::gtest
)

include(GoogleTest)
gtest_discover_tests(run_all_test)