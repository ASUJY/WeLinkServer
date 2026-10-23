aux_source_directory(./network NETWORK_TEST_SRC_LIST)
aux_source_directory(./mysql MYSQL_TEST_SRC_LIST)
aux_source_directory(./ TEST_SRC_LIST)

add_executable(run_all_test 
    ${TEST_SRC_LIST} 
    ${NETWORK_TEST_SRC_LIST}
    ${MYSQL_TEST_SRC_LIST}
)
target_link_libraries(run_all_test
    chatserver_lib
    GTest::gtest_main
    GTest::gtest
)

include(GoogleTest)
gtest_discover_tests(run_all_test)