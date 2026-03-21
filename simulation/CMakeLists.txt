add_library(simulation_lib STATIC
    ${CMAKE_CURRENT_SOURCE_DIR}/src/bank_server.cpp
)

target_include_directories(simulation_lib PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(simulation_lib PUBLIC raft)
set_project_warnings(simulation_lib)

add_executable(simulation
    ${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp
)

target_link_libraries(simulation PRIVATE simulation_lib)
set_project_warnings(simulation)

add_subdirectory(test)
