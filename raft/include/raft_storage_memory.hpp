#ifndef __RAFT_STORAGE_MEMORY_HPP__
#define __RAFT_STORAGE_MEMORY_HPP__

#include "raft_storage.hpp"

class raft_storage_memory final : public raft_storage
{
public:
	raft_storage_memory() = default;
	raft_storage_memory(const raft_storage_memory&) = delete;
	raft_storage_memory(raft_storage_memory&&) = delete;

	leader_term_t get_current_term() const override;

	void set_current_term(const leader_term_t current_term) override;

	node_id_t get_voted_for() const override;

	void set_voted_for(const node_id_t voted_for) override;

	log_entry_t get_log_entry(const log_entry_index_t index) const override;

	void push_log_entry(const log_entry_t) override;

private:
	leader_term_t m_current_term = 0;
	node_id_t m_voted_for = INVALID_NODE_ID;
	std::vector<log_entry_t> m_log;
};

#endif