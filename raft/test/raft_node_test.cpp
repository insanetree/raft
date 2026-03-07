#include <gtest/gtest.h>

#include "raft_node.hpp"
#include "raft_storage_memory.hpp"

class RaftNodeTest : public testing::Test
{
public:
	void SetUp() override { m_storage = std::make_shared<raft_storage_memory>(); }

	std::shared_ptr<raft_storage> m_storage;
	static constexpr node_id_t id = 1;
	static const std::vector<node_id_t> peers;
};

const std::vector<node_id_t> RaftNodeTest::peers = {2, 3};

TEST_F(RaftNodeTest, InitialStateIsFollower)
{
	raft_node node(id, peers, m_storage);
	EXPECT_EQ(node.get_state(), raft_node::node_state_e::FOLLOWER);
}

TEST_F(RaftNodeTest, StartElection)
{
	raft_node node(id, peers, 3, m_storage);
	EXPECT_EQ(node.get_state(), raft_node::node_state_e::FOLLOWER);

	node.tick();
	EXPECT_EQ(node.get_state(), raft_node::node_state_e::FOLLOWER);
	node.tick();
	EXPECT_EQ(node.get_state(), raft_node::node_state_e::FOLLOWER);
	node.tick();
	EXPECT_EQ(node.get_state(), raft_node::node_state_e::FOLLOWER);
	node.tick();
	EXPECT_EQ(node.get_state(), raft_node::node_state_e::CANDIDATE);

	std::vector<raft_message_t> messages = node.get_messages();
	ASSERT_TRUE(std::all_of(messages.begin(), messages.end(), [](const raft_message_t& msg) {
		return std::holds_alternative<request_vote_request>(msg);
	}));
	EXPECT_EQ(2, messages.size());
	std::vector<request_vote_request> messages_typed;
	std::transform(messages.begin(), messages.end(), std::back_inserter(messages_typed), [](const raft_message_t& msg) {
		return std::get<request_vote_request>(msg);
	});
	EXPECT_TRUE(std::all_of(messages_typed.begin(), messages_typed.end(), [](const request_vote_request& msg) {
		return msg.candidate_id == 1 && msg.candidate_term == 1;
	}));
}