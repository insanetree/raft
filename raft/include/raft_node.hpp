#ifndef __RAFT_NODE_HPP__
#define __RAFT_NODE_HPP__

#include "raft_message.hpp"
#include "raft_storage.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <utility>
#include <vector>

class raft_node
{
public:
	enum class node_state_e
	{
		FOLLOWER,
		LEADER,
		CANDIDATE
	};

	raft_node(node_id_t id,
	          std::vector<node_id_t> peers,
	          size_t election_threshold,
	          std::shared_ptr<raft_storage> storage);

	raft_node(node_id_t id, std::vector<node_id_t> peers, std::shared_ptr<raft_storage> storage);

	node_id_t get_id() const { return m_id; }

	node_state_e get_state() const { return m_state; }

	void tick();

	void step(const std::vector<raft_message_t>& messages);

    std::vector<raft_message_t> get_messages();

private:
	static size_t random_election_threshold();

	void start_election();

	void send_heartbeats();

	// Persistent state
	const node_id_t m_id;
	const std::vector<node_id_t> m_peers;
	node_state_e m_state;
	std::shared_ptr<raft_storage> m_storage;

	// Timeouts
	size_t m_election_threshold;
	size_t m_election_timeout;
	static constexpr size_t heartbeat_threshold = 50;
	size_t m_heartbeat_timeout;

	// Volatile state
	log_entry_index_t m_commit_index;
	log_entry_index_t m_last_applied;
	std::unordered_map<node_id_t, log_entry_index_t> m_next_index;
	std::unordered_map<node_id_t, log_entry_index_t> m_match_index;

	std::vector<raft_message_t> m_outbox;
};

#endif