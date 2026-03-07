#include "raft_node.hpp"

#include <cassert>
#include <random>
#include <vector>

size_t
raft_node::random_election_threshold()
{
	static std::random_device rd;
	static std::mt19937_64 gen(rd());
	static std::uniform_int_distribution<size_t> dist(3 * heartbeat_threshold, 5 * heartbeat_threshold);

	return dist(gen);
}

raft_node::raft_node(node_id_t id,
                     std::vector<node_id_t> peers,
                     size_t election_threshold,
                     std::shared_ptr<raft_storage> storage) :
	m_id(id),
	m_peers(peers),
	m_state(node_state_e::FOLLOWER),
	m_storage(storage),
	m_election_threshold(election_threshold),
	m_election_timeout(0),
	m_heartbeat_timeout(0)
{
}

raft_node::raft_node(node_id_t id, std::vector<node_id_t> peers, std::shared_ptr<raft_storage> storage) :
	raft_node(id, peers, random_election_threshold(), storage) {};

void
raft_node::tick()
{
	switch (m_state) {
	case node_state_e::FOLLOWER:
	case node_state_e::CANDIDATE:
		m_election_timeout++;
		if (m_election_timeout > m_election_threshold) {
			start_election();
		}
		break;
	case node_state_e::LEADER:
		m_heartbeat_timeout++;
		if (m_heartbeat_timeout > heartbeat_threshold) {
			send_heartbeats();
		}
		break;
	}
}

void
raft_node::start_election()
{
	assert(m_state != node_state_e::LEADER);
	m_state = node_state_e::CANDIDATE;
	m_election_timeout = 0;
	m_election_threshold = random_election_threshold();

	leader_term_t term = m_storage->get_current_term();
	m_storage->set_current_term(++term);
	log_entry_index_t last_log_index = m_storage->get_log_size();

	request_vote_request msg = {.candidate_term = term,
	                            .candidate_id = m_id,
	                            .last_log_index = last_log_index,
	                            .last_log_term = last_log_index ? m_storage->get_log_entry(last_log_index).term : 0ul};
	for (node_id_t peer : m_peers) {
		msg.dest = peer;
		m_outbox.push_back(msg);
	}
}

void
raft_node::send_heartbeats()
{
	assert(m_state == node_state_e::LEADER);
	m_heartbeat_timeout = 0;

	leader_term_t term = m_storage->get_current_term();
	log_entry_index_t last_log_index = m_storage->get_log_size();

	append_entry_request msg = {.leader_term = m_storage->get_current_term(),
	                            .leader_id = m_id,
	                            .prev_log_index = last_log_index,
	                            .prev_log_term = last_log_index ? m_storage->get_log_entry(last_log_index).term : 0ul,
	                            .entries = {},
	                            .leader_commit = m_commit_index};
	for (node_id_t peer : m_peers) {
		msg.dest = peer;
		m_outbox.push_back(msg);
	}
}

std::vector<raft_message_t>
raft_node::get_messages()
{
	std::vector<raft_message_t> sent_messages{std::move(m_outbox)};
	m_outbox = {};
	return std::move(sent_messages);
}