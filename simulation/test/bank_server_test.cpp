#include <gtest/gtest.h>

#include "raft/raft_storage_memory.hpp"
#include "simulation/bank_server.hpp"

TEST(BankServerTest, InitialStateIsFollower)
{
	std::shared_ptr<raft_storage_memory> storage = std::make_shared<raft_storage_memory>();
	bank_server server(1, {2, 3}, storage);
	EXPECT_EQ(server.get_state(), raft_node::node_state_e::FOLLOWER);
}
