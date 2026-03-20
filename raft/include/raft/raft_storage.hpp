#ifndef __RAFT_STORAGE_HPP__
#define __RAFT_STORAGE_HPP__

#include "raft/raft_types.hpp"

class raft_storage
{
public:
	virtual ~raft_storage() = default;

	virtual leader_term_t get_term() const = 0;

	virtual void set_term(const leader_term_t current_term) = 0;

	virtual node_id_t get_voted_for() const = 0;

	virtual void set_voted_for(const node_id_t voted_for) = 0;

	virtual const log_entry_t& get_log_entry(const log_entry_index_t index) const = 0;

	virtual log_entry_index_t get_log_size() const = 0;

	virtual void push_log_entry(const log_entry_t) = 0;

	virtual void pop_log_entry() = 0;

protected:
	raft_storage() = default;
};

#endif