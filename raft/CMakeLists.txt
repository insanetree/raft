add_library(raft SHARED
    ${CMAKE_CURRENT_SOURCE_DIR}/src/raft_node.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/raft_storage_memory.cpp
    )

target_include_directories(raft PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)