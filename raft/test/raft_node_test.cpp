#include <gtest/gtest.h>

#include "raft_node.hpp"
#include "raft_storage_memory.hpp"

TEST(RaftNodeTest, InitialStateIsFollower)
{
    node_id_t id = 1;
    std::vector<node_id_t> peers = {2,3};

    auto storage = std::make_shared<raft_storage_memory>();

    raft_node node(id, peers, storage);

    EXPECT_EQ(node.get_state(), raft_node::node_state_e::FOLLOWER);
}