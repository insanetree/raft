#include "raft/raft_node.hpp"

#include <algorithm>
#include <cassert>
#include <random>
#include <vector>

size_t
raft_node::random_election_threshold()
{
	static thread_local std::random_device rd;
	static thread_local std::mt19937_64 gen(rd());
	static thread_local std::uniform_int_distribution<size_t> dist(3 * heartbeat_threshold, 5 * heartbeat_threshold);

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
	const std::unique_lock lock{m_mutex};
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
		update_commit_index();
		break;
	}

	// Needed for special case where there is only one node in a cluster
	if (m_received_votes.size() > (m_peers.size() + 1) / 2) {
		state_transition(node_state_e::LEADER);
		send_heartbeats();
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

	for (node_id_t peer : m_peers) {
		append_entries(peer);
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
raft_node::append_entries(node_id_t follower_id)
{
	assert(m_state == node_state_e::LEADER);
	assert(follower_id != m_id);
	const log_entry_index_t last_log_entry = m_storage->get_log_size();
	const log_entry_index_t prev_log_index = m_next_index.at(follower_id) - 1ul;
	const leader_term_t prev_log_term = prev_log_index ? m_storage->get_log_entry(prev_log_index).term : 0ul;
	append_entries_request msg = {.dest = follower_id,
	                              .leader_term = m_storage->get_term(),
	                              .leader_id = m_id,
	                              .prev_log_index = prev_log_index,
	                              .prev_log_term = prev_log_term,
	                              .entries = {},
	                              .leader_commit = m_commit_index};

	// Note that we do <= here since indexing starts from 1
	for (log_entry_index_t index = m_next_index.at(follower_id); index <= last_log_entry; index++) {
		msg.entries.push_back(m_storage->get_log_entry(index));
	}
	m_outbox.push_back(std::move(msg));
}

void
raft_node::handle(const append_entries_request& message)
{
	assert(message.dest == m_id);

	append_entries_response response = {.dest = message.leader_id,
	                                    .follower_id = m_id,
	                                    .term = m_storage->get_term(),
	                                    .success = false,
	                                    .prev_log_index = 0,
	                                    .count = 0};

	// 1. Reply false if term < currentTerm
	if (message.leader_term < m_storage->get_term()) {
		m_outbox.push_back(response);
		return;
	}

	// 2. If term is newer step down
	if (message.leader_term > m_storage->get_term()) {
		update_term(message.leader_term);
	}

	// Convert candidate to follower if needed
	if (m_state != node_state_e::FOLLOWER) {
		state_transition(node_state_e::FOLLOWER);
	}

	m_election_timeout = 0;
	m_leader_id = message.leader_id;

	// 3. Check if log contains matching prev entry
	if (message.prev_log_index > m_storage->get_log_size()) {
		response.prev_log_index = m_storage->get_log_size();
		m_outbox.push_back(response);
		return;
	}

	if (message.prev_log_index > 0) {
		auto local_term = m_storage->get_log_entry(message.prev_log_index).term;
		if (local_term != message.prev_log_term) {
			response.prev_log_index = message.prev_log_index - 1;
			m_outbox.push_back(response);
			return;
		}
	}

	// At this point, logs match up to prev_log_index
	log_entry_index_t index = message.prev_log_index + 1;
	size_t i = 0;

	// 4. Resolve conflicts (ONLY where needed)
	while (i < message.entries.size()) {
		if (index <= m_storage->get_log_size()) {
			const auto& local = m_storage->get_log_entry(index);
			const auto& incoming = message.entries[i];

			if (local.term != incoming.term) {
				// Conflict found: delete everything after this index
				while (m_storage->get_log_size() >= index) {
					// NEVER delete committed entries
					assert(m_storage->get_log_size() > m_commit_index);
					m_storage->pop_log_entry();
				}
				break;
			}
		} else {
			break;
		}
		index++;
		i++;
	}

	// 5. Append any new entries
	size_t appended = 0;
	for (; i < message.entries.size(); i++) {
		m_storage->push_log_entry(message.entries[i]);
		appended++;
	}

	response.success = true;
	response.prev_log_index = message.prev_log_index;
	response.count = appended;

	// 6. Update commit index
	if (message.leader_commit > m_commit_index) {
		m_commit_index = std::min(message.leader_commit, m_storage->get_log_size());
	}

	// 7. Apply committed entries
	while (m_last_applied < m_commit_index) {
		m_state_machine->apply(m_storage->get_log_entry(++m_last_applied));
	}

	m_outbox.push_back(response);
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
		append_entries(message.follower_id);
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

	if (m_received_votes.size() > (m_peers.size() + 1) / 2) {
		state_transition(node_state_e::LEADER);
		send_heartbeats();
	}
}

void
raft_node::step(const raft_message_t& message)
{
	const std::unique_lock lock{m_mutex};
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
	const std::unique_lock lock{m_mutex};
	std::vector<raft_message_t> sent_messages{std::move(m_outbox)};
	m_outbox = {};
	return sent_messages;
}

log_entry_index_t
raft_node::append_log(std::vector<uint8_t> command)
{
	const std::unique_lock lock{m_mutex};
	assert(m_state == node_state_e::LEADER);
	m_storage->push_log_entry({.term = m_storage->get_term(), .command = std::move(command)});
	return m_storage->get_log_size();
}
