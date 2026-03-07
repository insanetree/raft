add_executable(raft_tests
    raft_node_test.cpp
)

target_link_libraries(raft_tests
    PRIVATE
        raft
        GTest::gtest_main
)

include(GoogleTest)
gtest_discover_tests(raft_tests)