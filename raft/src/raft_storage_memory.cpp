#include "raft_storage_memory.hpp"

leader_term_t
raft_storage_memory::get_current_term() const
{
	return m_current_term;
}

void
raft_storage_memory::set_current_term(const leader_term_t current_term)
{
	m_current_term = current_term;
}

node_id_t
raft_storage_memory::get_voted_for() const
{
	return m_voted_for;
}

void
raft_storage_memory::set_voted_for(const node_id_t voted_for)
{
	m_voted_for = voted_for;
}

const log_entry_t&
raft_storage_memory::get_log_entry(const log_entry_index_t index) const
{
	return m_log.at(index - 1);
}

log_entry_index_t
raft_storage_memory::get_log_size() const
{
	return m_log.size();
}

void
raft_storage_memory::push_log_entry(const log_entry_t log_entry)
{
	m_log.push_back(log_entry);
}