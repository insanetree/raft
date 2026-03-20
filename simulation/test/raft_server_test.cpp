#include <gtest/gtest.h>

#include "simulation/raft_server.hpp"

TEST(RaftServerTest, InitialStateIsFollower)
{
	raft_server server(1, {2, 3});
	EXPECT_EQ(server.get_state(), raft_node::node_state_e::FOLLOWER);
}
