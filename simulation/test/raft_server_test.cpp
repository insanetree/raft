#include <gtest/gtest.h>

#include "simulation/raft_server.hpp"

TEST(RaftServerTest, InitialStateIsFollower)
{
	std::shared_ptr<raft_storage_memory> storage = std::make_shared<raft_storage_memory>();
	raft_server server(1, {2, 3}, storage);
	EXPECT_EQ(server.get_state(), raft_node::node_state_e::FOLLOWER);
}
