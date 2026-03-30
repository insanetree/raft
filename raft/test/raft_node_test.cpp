#include <algorithm>

#include <gtest/gtest.h>

#include "raft/raft_node.hpp"
#include "raft/raft_state_machine.hpp"
#include "raft/raft_storage_memory.hpp"

class noop_state_machine : public raft_state_machine
{
public:
	void apply(log_entry_t) override {}
};

class RaftNodeTest : public testing::Test
{
public:
	void SetUp() override
	{
		m_storage = std::make_shared<raft_storage_memory>();
		m_state_machine = std::make_shared<noop_state_machine>();
	}

	std::shared_ptr<raft_storage> m_storage;
	std::shared_ptr<raft_state_machine> m_state_machine;
	static constexpr node_id_t id = 1;
	static const std::vector<node_id_t> peers;
};

const std::vector<node_id_t> RaftNodeTest::peers = {2, 3};

TEST_F(RaftNodeTest, InitialStateIsFollower)
{
	raft_node node(id, peers, m_storage, m_state_machine);
	EXPECT_EQ(node.get_state(), raft_node::node_state_e::FOLLOWER);
	EXPECT_EQ(node.get_term(), 0);
}

TEST_F(RaftNodeTest, StartElection)
{
	raft_node node(id, peers, 3, m_storage, m_state_machine);
	EXPECT_EQ(node.get_state(), raft_node::node_state_e::FOLLOWER);
	EXPECT_EQ(node.get_term(), 0);

	node.tick();
	EXPECT_EQ(node.get_state(), raft_node::node_state_e::FOLLOWER);
	node.tick();
	EXPECT_EQ(node.get_state(), raft_node::node_state_e::FOLLOWER);
	node.tick();
	EXPECT_EQ(node.get_state(), raft_node::node_state_e::FOLLOWER);
	node.tick();
	EXPECT_EQ(node.get_state(), raft_node::node_state_e::CANDIDATE);
	EXPECT_EQ(node.get_term(), 1);

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

TEST_F(RaftNodeTest, CandidateIncrementsTerm)
{
	raft_node node(id, peers, 3, m_storage, m_state_machine);
	EXPECT_EQ(node.get_term(), 0);

	for (int i = 0; i < 4; i++) {
		node.tick();
	}
	EXPECT_EQ(node.get_state(), raft_node::node_state_e::CANDIDATE);
	EXPECT_EQ(node.get_term(), 1);
}

TEST_F(RaftNodeTest, CandidateVotesForItself)
{
	raft_node node(id, peers, 3, m_storage, m_state_machine);

	for (int i = 0; i < 4; i++) {
		node.tick();
	}

	EXPECT_EQ(node.get_voted_for(), id);
}

TEST_F(RaftNodeTest, CandidateSendsVoteRequests)
{
	raft_node node(id, peers, 3, m_storage, m_state_machine);

	for (int i = 0; i < 4; i++) {
		node.tick();
	}

	auto msgs = node.get_messages();

	EXPECT_EQ(msgs.size(), peers.size());

	EXPECT_TRUE(
		std::all_of(msgs.begin(), msgs.end(), [](auto& m) { return std::holds_alternative<request_vote_request>(m); }));
}

TEST_F(RaftNodeTest, VoteRequestHasCorrectCandidateId)
{
	raft_node node(id, peers, 3, m_storage, m_state_machine);

	for (int i = 0; i < 4; i++) {
		node.tick();
	}

	auto msgs = node.get_messages();

	for (auto& m : msgs) {
		auto req = std::get<request_vote_request>(m);
		EXPECT_EQ(req.candidate_id, id);
	}
}

TEST_F(RaftNodeTest, FollowerGrantsVote)
{
	raft_node node(id, peers, m_storage, m_state_machine);

	request_vote_request req{1, 1, 2, 0, 0};

	node.step(req);

	auto msgs = node.get_messages();

	ASSERT_EQ(msgs.size(), 1);

	auto resp = std::get<request_vote_response>(msgs[0]);

	EXPECT_TRUE(resp.vote_granted);
}

TEST_F(RaftNodeTest, RejectSecondVoteSameTerm)
{
	raft_node node(id, peers, m_storage, m_state_machine);

	request_vote_request r1{1, 1, 2, 0, 0};
	request_vote_request r2{1, 1, 3, 0, 0};

	node.step(r1);
	node.step(r2);

	auto msgs = node.get_messages();

	auto resp2 = std::get<request_vote_response>(msgs[1]);

	EXPECT_FALSE(resp2.vote_granted);
}

TEST_F(RaftNodeTest, RejectVoteStaleTerm)
{
	raft_node node(id, peers, m_storage, m_state_machine);

	request_vote_request req{
		.dest = id, .candidate_term = 0, .candidate_id = 2, .last_log_index = 0, .last_log_term = 0};

	node.step(req);

	auto resp = std::get<request_vote_response>(node.get_messages()[0]);

	EXPECT_FALSE(resp.vote_granted);
}

TEST_F(RaftNodeTest, CandidateBecomesLeader)
{
	raft_node node(id, peers, 3, m_storage, m_state_machine);

	for (int i = 0; i < 4; i++) {
		node.tick();
	}

	EXPECT_EQ(node.get_state(), raft_node::node_state_e::CANDIDATE);

	request_vote_response resp{.dest = id, .src = 2, .term = 1, .vote_granted = true};

	node.step(resp);

	EXPECT_EQ(node.get_state(), raft_node::node_state_e::LEADER);
}

TEST_F(RaftNodeTest, LeaderSendsHeartbeats)
{
	raft_node node(id, peers, m_storage, m_state_machine);

	for (int i = 0; i < 51; i++) {
		node.tick();
	}

	request_vote_response resp{.dest = id, .src = 2, .term = 1, .vote_granted = true};
	node.step(resp);

	auto msgs = node.get_messages();

	EXPECT_TRUE(std::all_of(
		msgs.begin(), msgs.end(), [](auto& m) { return std::holds_alternative<append_entries_request>(m); }));
}

TEST_F(RaftNodeTest, FollowerAcceptsHeartbeat)
{
	raft_node node(id, peers, m_storage, m_state_machine);

	append_entries_request req{1, 1, 2, 0, 0, {}, 0};

	node.step(req);

	auto resp = std::get<append_entries_response>(node.get_messages()[0]);

	EXPECT_TRUE(resp.success);
}

TEST_F(RaftNodeTest, RejectAppendEntriesOldTerm)
{
	raft_node node(id, peers, m_storage, m_state_machine);

	append_entries_request req{1, 1, 2, 0, 0, {}, 0};

	node.step(req);

	auto resp = std::get<append_entries_response>(node.get_messages()[0]);

	EXPECT_TRUE(resp.success);
	EXPECT_EQ(node.get_term(), 1);

	req.leader_term = 0;
	node.step(req);
	resp = std::get<append_entries_response>(node.get_messages()[0]);
	EXPECT_FALSE(resp.success);
	EXPECT_EQ(node.get_term(), 1);
}

TEST_F(RaftNodeTest, LeaderStepsDownOnHigherTermAppend)
{
	raft_node node(id, peers, 3, m_storage, m_state_machine);

	for (int i = 0; i < 4; i++) {
		node.tick();
	}
	node.step(request_vote_response{.dest = id, .src = 2, .term = 1, .vote_granted = true});

	append_entries_request req{1, 5, 2, 0, 0, {}, 0};

	node.step(req);

	EXPECT_EQ(node.get_state(), raft_node::node_state_e::FOLLOWER);
}

TEST_F(RaftNodeTest, CandidateStepsDownOnHeartbeat)
{
	raft_node node(id, peers, 3, m_storage, m_state_machine);

	for (int i = 0; i < 4; i++) {
		node.tick();
	}

	append_entries_request req{1, 1, 2, 0, 0, {}, 0};

	node.step(req);

	EXPECT_EQ(node.get_state(), raft_node::node_state_e::FOLLOWER);
}

TEST_F(RaftNodeTest, HeartbeatResetsElectionTimeout)
{
	raft_node node(id, peers, 3, m_storage, m_state_machine);

	node.tick();

	append_entries_request hb{1, 1, 2, 0, 0, {}, 0};

	node.step(hb);

	node.tick();
	node.tick();

	EXPECT_EQ(node.get_state(), raft_node::node_state_e::FOLLOWER);
}

TEST_F(RaftNodeTest, CandidateRestartsElection)
{
	raft_node node(id, peers, 3, m_storage, m_state_machine);

	for (size_t i = 0; i < node.get_election_threshold() + 1; i++) {
		node.tick();
	}

	EXPECT_EQ(node.get_state(), raft_node::node_state_e::CANDIDATE);

	for (size_t i = 0; i < node.get_election_threshold() + 1; i++) {
		node.tick();
	}

	EXPECT_EQ(node.get_term(), 2);
}

TEST_F(RaftNodeTest, SingleNodeCanBecomeLeader)
{
	raft_node node(id, {}, 3, m_storage, m_state_machine);

	for (int i = 0; i < 4; i++) {
		node.tick();
	}

	EXPECT_EQ(node.get_state(), raft_node::node_state_e::LEADER);
}
