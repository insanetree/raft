#include <algorithm>
#include <array>
#include <thread>

#include <gtest/gtest.h>

#include "raft/raft_node.hpp"
#include "raft/raft_state_machine.hpp"
#include "raft/raft_storage_memory.hpp"

class noop_state_machine : public raft_state_machine
{
public:
	void apply(log_entry_t) override {}
};

class RaftClusterTest : public testing::Test
{
public:
	void SetUp() override
	{
		for (size_t i = 0; i < 3; i++) {
			storages[i] = std::make_shared<raft_storage_memory>();
			state_machines[i] = std::make_shared<noop_state_machine>();
			std::vector<node_id_t> peers;
			for (size_t j = 0; j < 3; j++) {
				if (j != i) {
					peers.push_back(j + 1);
				}
			}
			nodes[i] = std::make_shared<raft_node>(i + 1, peers, storages[i], state_machines[i]);
		}
	}

	void TearDown() override
	{
		for (size_t i = 0; i < 3; i++) {
			stop_node(i);
		}
	}

	void tick_node(size_t index)
	{
		std::vector<raft_message_t> messages;
		{
			std::lock_guard lock{message_queue_mutexes[index]};
			messages.swap(message_queues[index]);
		}
		nodes[index]->step(messages);
		nodes[index]->tick();
		auto outgoing = nodes[index]->get_messages();
		for (auto& msg : outgoing) {
			size_t dest_index = get_dest(msg) - 1;
			std::lock_guard lock{message_queue_mutexes[dest_index]};
			message_queues[dest_index].push_back(std::move(msg));
		}
	}

	void drive_node(size_t index)
	{
		while (is_node_running(index)) {
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
			tick_node(index);
		}
		nodes[index].reset();
		state_machines[index].reset();
	}

	void start_node(size_t index)
	{
		if (!nodes[index]) {
			state_machines[index] = std::make_shared<noop_state_machine>();
			std::vector<node_id_t> peers;
			for (size_t j = 0; j < 3; j++) {
				if (j != index) {
					peers.push_back(j + 1);
				}
			}
			nodes[index] = std::make_shared<raft_node>(index + 1, peers, storages[index], state_machines[index]);
		}
		std::lock_guard lock{run_signal_mutex[index]};
		std::lock_guard lock2(message_queue_mutexes[index]);
		run_signal[index] = true;
		std::vector<raft_message_t> empty_queue{};
		std::swap(empty_queue, message_queues[index]);
		threads[index] = std::jthread([this, index] { drive_node(index); });
	}

	void stop_node(size_t index)
	{
		std::lock_guard lock1{run_signal_mutex[index]};
		std::lock_guard lock2(message_queue_mutexes[index]);
		run_signal[index] = false;
		std::vector<raft_message_t> empty_queue{};
		std::swap(empty_queue, message_queues[index]);
	}

	bool is_node_running(size_t index)
	{
		std::lock_guard lock{run_signal_mutex[index]};
		return run_signal[index];
	}

	void start_all()
	{
		for (size_t i = 0; i < 3; i++) {
			start_node(i);
		}
	}

	void stop_all()
	{
		for (size_t i = 0; i < 3; i++) {
			stop_node(i);
		}
	}

	size_t find_leader()
	{
		for (size_t i = 0; i < 3; i++) {
			if (is_node_running(i) && nodes[i]->get_state() == raft_node::node_state_e::LEADER) {
				return i;
			}
		}
		return SIZE_MAX;
	}

	void restart_node(size_t index)
	{
		stop_node(index);
		nodes[index].reset();
		state_machines[index].reset();
		start_node(index);
	}

	std::array<std::shared_ptr<raft_node>, 3> nodes;
	std::array<bool, 3> run_signal;
	std::array<std::mutex, 3> run_signal_mutex;
	std::array<std::shared_ptr<raft_storage>, 3> storages;
	std::array<std::shared_ptr<raft_state_machine>, 3> state_machines;
	std::array<std::mutex, 3> message_queue_mutexes;
	std::array<std::vector<raft_message_t>, 3> message_queues;
	std::array<std::jthread, 3> threads;
};

TEST_F(RaftClusterTest, OneLeaderEmerges)
{
	for (size_t i = 0; i < 3; i++) {
		ASSERT_EQ(nodes[i]->get_state(), raft_node::node_state_e::FOLLOWER);
	}

	for (size_t i = 0; i < 3; i++) {
		start_node(i);
	}

	std::this_thread::sleep_for(std::chrono::seconds(2));

	size_t leader_count = 0;
	size_t follower_count = 0;
	for (size_t i = 0; i < 3; i++) {
		if (nodes[i]->get_state() == raft_node::node_state_e::LEADER) {
			leader_count++;
		} else if (nodes[i]->get_state() == raft_node::node_state_e::FOLLOWER) {
			follower_count++;
		}
	}
	ASSERT_EQ(leader_count, 1);
	ASSERT_EQ(follower_count, 2);
}

TEST_F(RaftClusterTest, TermsEqualize)
{
	start_node(0);
	std::this_thread::sleep_for(std::chrono::seconds(1));
	ASSERT_GT(nodes[0]->get_term(), nodes[1]->get_term());
	ASSERT_GT(nodes[0]->get_term(), nodes[2]->get_term());

	start_node(1);
	start_node(2);
	std::this_thread::sleep_for(std::chrono::seconds(1));
	ASSERT_EQ(nodes[0]->get_term(), nodes[1]->get_term());
	ASSERT_EQ(nodes[0]->get_term(), nodes[2]->get_term());
}

TEST_F(RaftClusterTest, AllNodesAgreeOnLeader)
{
	start_all();
	std::this_thread::sleep_for(std::chrono::seconds(2));

	size_t leader = find_leader();
	ASSERT_NE(leader, SIZE_MAX);
	node_id_t leader_id = nodes[leader]->get_id();

	for (size_t i = 0; i < 3; i++) {
		if (i != leader) {
			ASSERT_EQ(nodes[i]->get_state(), raft_node::node_state_e::FOLLOWER);
			ASSERT_EQ(nodes[i]->get_leader_id(), leader_id);
		}
	}
}

TEST_F(RaftClusterTest, LeaderReelectionAfterFailure)
{
	start_all();
	std::this_thread::sleep_for(std::chrono::seconds(2));

	size_t old_leader = find_leader();
	ASSERT_NE(old_leader, SIZE_MAX);

	stop_node(old_leader);
	std::this_thread::sleep_for(std::chrono::seconds(2));

	size_t new_leader = find_leader();
	ASSERT_NE(new_leader, SIZE_MAX);
	ASSERT_NE(new_leader, old_leader);
}

TEST_F(RaftClusterTest, LogReplicatesToFollowers)
{
	start_all();
	std::this_thread::sleep_for(std::chrono::seconds(2));

	size_t leader = find_leader();
	ASSERT_NE(leader, SIZE_MAX);

	std::vector<uint8_t> cmd = {1, 2, 3, 4};
	nodes[leader]->append_log(cmd);

	std::this_thread::sleep_for(std::chrono::seconds(2));

	for (size_t i = 0; i < 3; i++) {
		ASSERT_GE(storages[i]->get_log_size(), 1);
		ASSERT_EQ(storages[i]->get_log_entry(storages[i]->get_log_size()).command, cmd);
	}
}

TEST_F(RaftClusterTest, NodeRejoinsAndCatchesUp)
{
	start_all();
	std::this_thread::sleep_for(std::chrono::seconds(2));

	size_t leader = find_leader();
	ASSERT_NE(leader, SIZE_MAX);

	// Pick a follower to take down
	size_t follower = (leader + 1) % 3;
	stop_node(follower);

	// Append entries while follower is down
	std::vector<uint8_t> cmd1 = {0xAA};
	std::vector<uint8_t> cmd2 = {0xBB};
	nodes[leader]->append_log(cmd1);
	nodes[leader]->append_log(cmd2);
	std::this_thread::sleep_for(std::chrono::seconds(1));

	// Restart the follower
	restart_node(follower);
	std::this_thread::sleep_for(std::chrono::seconds(2));

	// Follower should have caught up
	ASSERT_EQ(*storages[follower], *storages[leader]);
}

TEST_F(RaftClusterTest, MinorityCannotElectLeader)
{
	start_node(0);
	std::this_thread::sleep_for(std::chrono::seconds(2));

	// Single node out of 3 cannot form a majority
	ASSERT_NE(nodes[0]->get_state(), raft_node::node_state_e::LEADER);
}

TEST_F(RaftClusterTest, StressConcurrentAppends)
{
	start_all();
	std::this_thread::sleep_for(std::chrono::seconds(2));

	size_t leader = find_leader();
	ASSERT_NE(leader, SIZE_MAX);

	constexpr size_t NUM_APPENDS = 20;
	for (size_t i = 0; i < NUM_APPENDS; i++) {
		std::vector<uint8_t> cmd = {static_cast<uint8_t>(i), static_cast<uint8_t>(i >> 8)};
		nodes[leader]->append_log(cmd);
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}

	std::this_thread::sleep_for(std::chrono::seconds(2));

	for (size_t i = 1; i < 3; i++) {
		ASSERT_EQ(*storages[i], *storages[0]) << "Node " << i << " storage mismatch";
	}
}

TEST_F(RaftClusterTest, StressRepeatedLeaderFailover)
{
	start_all();
	std::this_thread::sleep_for(std::chrono::seconds(2));

	constexpr size_t NUM_FAILOVERS = 3;
	for (size_t round = 0; round < NUM_FAILOVERS; round++) {
		size_t leader = find_leader();
		ASSERT_NE(leader, SIZE_MAX) << "No leader in round " << round;

		std::vector<uint8_t> cmd = {static_cast<uint8_t>(round)};
		nodes[leader]->append_log(cmd);
		std::this_thread::sleep_for(std::chrono::milliseconds(500));

		stop_node(leader);
		std::this_thread::sleep_for(std::chrono::seconds(2));

		size_t new_leader = find_leader();
		ASSERT_NE(new_leader, SIZE_MAX) << "No new leader after failover in round " << round;
		ASSERT_NE(new_leader, leader);

		start_node(leader);
		std::this_thread::sleep_for(std::chrono::seconds(1));
	}

	std::this_thread::sleep_for(std::chrono::seconds(2));

	for (size_t i = 1; i < 3; i++) {
		ASSERT_EQ(*storages[i], *storages[0]) << "Node " << i << " storage mismatch";
	}
}
