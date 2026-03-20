add_executable(simulation_tests
    raft_server_test.cpp
)

target_link_libraries(simulation_tests
    PRIVATE
        simulation_lib
        GTest::gtest_main
)

set_project_warnings(simulation_tests)

include(GoogleTest)
gtest_discover_tests(simulation_tests)
