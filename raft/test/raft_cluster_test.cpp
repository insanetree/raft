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
	}

	void start_node(size_t index)
	{
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
