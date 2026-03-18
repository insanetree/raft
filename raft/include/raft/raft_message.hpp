#ifndef __RAFT_MESSAGE_HPP__
#define __RAFT_MESSAGE_HPP__

#include "raft_types.hpp"

#include <variant>

struct append_entry_request
{
	node_id_t dest;
	leader_term_t leader_term;
	node_id_t leader_id;
	log_entry_index_t prev_log_index;
	leader_term_t prev_log_term;
	std::vector<log_entry_t> entries;
	log_entry_index_t leader_commit;
};

struct append_entry_response
{
	node_id_t dest;
	node_id_t follower_id;
	leader_term_t term;
	bool success;
};

struct request_vote_request
{
	node_id_t dest;
	node_id_t src;
	leader_term_t candidate_term;
	node_id_t candidate_id;
	log_entry_index_t last_log_index;
	leader_term_t last_log_term;
};

struct request_vote_response
{
	node_id_t dest;
	leader_term_t term;
	bool vote_granted;
};

using raft_message_t =
	std::variant<append_entry_request, append_entry_response, request_vote_request, request_vote_response>;

#endif