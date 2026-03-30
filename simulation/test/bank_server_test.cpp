#include <gtest/gtest.h>

#include "raft/raft_storage_memory.hpp"
#include "simulation/bank_server.hpp"

class BankServerTest : public ::testing::Test
{
protected:
	std::shared_ptr<raft_storage_memory> m_storage;

	void SetUp() override { m_storage = std::make_shared<raft_storage_memory>(); }
};

TEST_F(BankServerTest, InitialStateIsFollower)
{
	bank_server server(1, {2, 3}, m_storage);
	EXPECT_EQ(server.get_state(), raft_node::node_state_e::FOLLOWER);
}
