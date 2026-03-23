#include "raft/raft_node.hpp"

#include <algorithm>
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
                     std::shared_ptr<raft_storage> storage,
                     std::shared_ptr<raft_state_machine> state_machine) :
	m_id(id),
	m_peers(peers),
	m_state(node_state_e::FOLLOWER),
	m_storage(storage),
	m_state_machine(state_machine),
	m_election_threshold(election_threshold),
	m_election_timeout(0),
	m_heartbeat_timeout(0),
	m_leader_id(INVALID_NODE_ID),
	m_commit_index(0),
	m_last_applied(0)
{
}

raft_node::raft_node(node_id_t id,
                     std::vector<node_id_t> peers,
                     std::shared_ptr<raft_storage> storage,
                     std::shared_ptr<raft_state_machine> state_machine) :
	raft_node(id, peers, random_election_threshold(), storage, state_machine) {};

leader_term_t
raft_node::get_last_log_term() const
{
	log_entry_index_t last_log_index = m_storage->get_log_size();
	return last_log_index ? m_storage->get_log_entry(last_log_index).term : 0ul;
}

void
raft_node::state_transition(node_state_e state)
{
	// Clear volatile state
	if (m_state == node_state_e::LEADER) {
		m_next_index.clear();
		m_match_index.clear();
	} else if (m_state == node_state_e::CANDIDATE) {
		m_received_votes.clear();
	}

	// Initialize volatile state
	m_heartbeat_timeout = 0;
	m_election_timeout = 0;
	if (state == node_state_e::LEADER) {
		m_leader_id = m_id;
		for (node_id_t peer : m_peers) {
			m_next_index.emplace(peer, m_storage->get_log_size() + 1);
			m_match_index.emplace(peer, 0);
		}
	} else if (state == node_state_e::CANDIDATE) {
		m_leader_id = INVALID_NODE_ID;
		m_received_votes.insert(m_id);
	}

	// Switch state
	m_state = state;
}

void
raft_node::update_term(const leader_term_t new_term)
{
	assert(m_storage->get_term() < new_term);

	state_transition(node_state_e::FOLLOWER);
	m_storage->set_term(new_term);
	m_storage->set_voted_for(INVALID_NODE_ID);
}

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
	state_transition(node_state_e::CANDIDATE);
	m_storage->set_voted_for(m_id);
	m_election_timeout = 0;
	m_election_threshold = random_election_threshold();

	leader_term_t term = m_storage->get_term();
	m_storage->set_term(++term);
	log_entry_index_t last_log_index = m_storage->get_log_size();

	request_vote_request msg = {.dest = 0,
	                            .candidate_term = term,
	                            .candidate_id = m_id,
	                            .last_log_index = last_log_index,
	                            .last_log_term = get_last_log_term()};
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

	append_entries_request msg = {.dest = 0,
	                              .leader_term = m_storage->get_term(),
	                              .leader_id = m_id,
	                              .prev_log_index = 0,
	                              .prev_log_term = 0,
	                              .entries = {},
	                              .leader_commit = m_commit_index};
	for (node_id_t peer : m_peers) {
		msg.dest = peer;
		msg.prev_log_index = m_next_index.at(peer) - 1ul;
		msg.prev_log_term = msg.prev_log_index ? m_storage->get_log_entry(msg.prev_log_index).term : 0ul;
		m_outbox.push_back(msg);
	}
}

void
raft_node::update_commit_index()
{
	std::vector<log_entry_index_t> match_indices;
	match_indices.reserve(m_peers.size() + 1);
	match_indices.push_back(m_storage->get_log_size());
	for (const auto& [peer, idx] : m_match_index) {
		match_indices.push_back(idx);
	}

	std::sort(match_indices.begin(), match_indices.end(), std::greater<>());

	log_entry_index_t N = match_indices[match_indices.size() / 2];

	if (N > m_commit_index && N > 0 && m_storage->get_log_entry(N).term == m_storage->get_term()) {
		m_commit_index = N;
	}

	while (m_last_applied < m_commit_index) {
		m_state_machine->apply(m_storage->get_log_entry(++m_last_applied));
	}
}

void
raft_node::handle(const append_entries_request& message)
{
	assert(message.dest == m_id);

	if (message.leader_term > m_storage->get_term()) {
		update_term(message.leader_term);
	}

	if (message.leader_term == m_storage->get_term() && m_state == node_state_e::CANDIDATE) {
		state_transition(node_state_e::FOLLOWER);
	}

	if (m_state != node_state_e::FOLLOWER) {
		return;
	}

	m_election_timeout = 0;
	m_leader_id = message.leader_id;

	append_entries_response response = {.dest = message.leader_id,
	                                    .follower_id = m_id,
	                                    .term = m_storage->get_term(),
	                                    .success = false,
	                                    .prev_log_index = m_storage->get_log_size(),
	                                    .count = 0ul};

	if (message.leader_term < m_storage->get_term()) {
		response.success = false;
		goto respond;
	}

	if (message.prev_log_index > m_storage->get_log_size()) {
		response.success = false;
		goto respond;
	}

	if (message.prev_log_index > 0 && m_storage->get_log_entry(message.prev_log_index).term != message.prev_log_term) {
		response.success = false;
		goto respond;
	}

	while (message.prev_log_index < m_storage->get_log_size()) {
		m_storage->pop_log_entry();
	}

	response.success = true;

	for (const log_entry_t& entry : message.entries) {
		m_storage->push_log_entry(entry);
	}

	response.count = message.entries.size();

	if (message.leader_commit > m_commit_index) {
		m_commit_index = std::min(m_storage->get_log_size(), message.leader_commit);
	}

	while (m_last_applied < m_commit_index) {
		m_state_machine->apply(m_storage->get_log_entry(++m_last_applied));
	}

respond:
	m_outbox.push_back(response);
	return;
}

void
raft_node::handle(const append_entries_response& message)
{
	assert(message.dest == m_id);

	if (message.term > m_storage->get_term()) {
		update_term(message.term);
	}

	if (m_state != node_state_e::LEADER) {
		return;
	}

	if (!message.success) {
		m_next_index.at(message.follower_id) = std::max(m_next_index.at(message.follower_id) - 1ul, 1ul);
		return;
	}

	log_entry_index_t match = message.prev_log_index + message.count;
	m_match_index.at(message.follower_id) = std::max(m_match_index.at(message.follower_id), match);
	m_next_index.at(message.follower_id) = m_match_index.at(message.follower_id) + 1;
	update_commit_index();
}

void
raft_node::handle(const request_vote_request& message)
{
	assert(message.dest == m_id);
	request_vote_response response = {
		.dest = message.candidate_id, .src = m_id, .term = m_storage->get_term(), .vote_granted = false};

	if (message.candidate_term <= m_storage->get_term()) {
		goto respond;
	}

	if (message.candidate_term > m_storage->get_term()) {
		update_term(message.candidate_term);
	}

	if (m_storage->get_voted_for() != INVALID_NODE_ID && m_storage->get_voted_for() != message.candidate_id) {
		goto respond;
	}

	if (message.last_log_term < get_last_log_term()) {
		goto respond;
	}

	if (message.last_log_term == get_last_log_term() && message.last_log_index < m_storage->get_log_size()) {
		goto respond;
	}

	response.vote_granted = true;
	m_storage->set_voted_for(message.candidate_id);
respond:
	m_outbox.push_back(response);
	return;
}

void
raft_node::handle(const request_vote_response& message)
{
	assert(message.dest == m_id);
	if (message.term > m_storage->get_term()) {
		update_term(message.term);
		return;
	}

	// A candidate can get half + 1 votes, turn to LEADER and still receive votes from the election it won.
	if (m_state != node_state_e::CANDIDATE) {
		return;
	}

	if (message.vote_granted) {
		m_received_votes.insert(message.src);
	}

	if (m_received_votes.size() > m_peers.size() / 2) {
		state_transition(node_state_e::LEADER);
		send_heartbeats();
	}
}

void
raft_node::step(const raft_message_t& message)
{
	std::visit([this](const auto& m) { handle(m); }, message);
}

void
raft_node::step(const std::vector<raft_message_t>& messages)
{
	std::for_each_n(messages.begin(), messages.size(), [this](const raft_message_t& msg) { step(msg); });
}

std::vector<raft_message_t>
raft_node::get_messages()
{
	std::vector<raft_message_t> sent_messages{std::move(m_outbox)};
	m_outbox = {};
	return sent_messages;
}
