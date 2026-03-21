#ifndef __RAFT_NODE_HPP__
#define __RAFT_NODE_HPP__

#include "raft/raft_message.hpp"
#include "raft/raft_state_machine.hpp"
#include "raft/raft_storage.hpp"
#include "raft/raft_types.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <unordered_map>
#include <unordered_set>
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
	          std::shared_ptr<raft_storage> storage,
	          std::shared_ptr<raft_state_machine> state_machine);

	raft_node(node_id_t id,
	          std::vector<node_id_t> peers,
	          std::shared_ptr<raft_storage> storage,
	          std::shared_ptr<raft_state_machine> state_machine);

	node_id_t get_id() const { return m_id; }

	node_state_e get_state() const { return m_state; }

	leader_term_t get_term() const { return m_storage->get_term(); };

	node_id_t get_voted_for() const { return m_storage->get_voted_for(); }

	size_t get_election_threshold() const { return m_election_threshold; }

	log_entry_index_t get_commit_index() const { return m_commit_index; }

	log_entry_index_t get_last_applied() const { return m_last_applied; }

	void tick();

	void step(const raft_message_t& message);

	void step(const std::vector<raft_message_t>& messages);

	std::vector<raft_message_t> get_messages();

private:
	static size_t random_election_threshold();

	leader_term_t get_last_log_term() const;

	void state_transition(node_state_e state);

	/// @brief Updates the term and nullifies voted_for
	void update_term(const leader_term_t new_term);

	void start_election();

	void send_heartbeats();

	void update_commit_index();

	void handle(const append_entries_request&);
	void handle(const append_entries_response&);
	void handle(const request_vote_request&);
	void handle(const request_vote_response&);

	// Persistent state
	const node_id_t m_id;
	const std::vector<node_id_t> m_peers;
	node_state_e m_state;
	std::shared_ptr<raft_storage> m_storage;
	std::shared_ptr<raft_state_machine> m_state_machine;

	// Timeouts
	size_t m_election_threshold;
	size_t m_election_timeout;
	static constexpr size_t heartbeat_threshold = 50;
	size_t m_heartbeat_timeout;

	// Volatile state
	log_entry_index_t m_commit_index;
	log_entry_index_t m_last_applied;

	// Leader volatile state
	std::unordered_map<node_id_t, log_entry_index_t> m_next_index;
	std::unordered_map<node_id_t, log_entry_index_t> m_match_index;
	// Candidate volatile state
	std::unordered_set<node_id_t> m_received_votes;

	std::vector<raft_message_t> m_outbox;
};

#endif
