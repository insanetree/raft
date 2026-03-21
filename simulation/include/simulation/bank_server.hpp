#ifndef __BANK_SERVER_HPP__
#define __BANK_SERVER_HPP__

#include "raft/raft_node.hpp"
#include "raft/raft_state_machine.hpp"
#include "raft/raft_storage_memory.hpp"

#include <memory>
#include <vector>

struct bank_transaction
{
	size_t from;
	size_t to;
	size_t amount;
};

class bank_server
{
public:
	bank_server(node_id_t id, std::vector<node_id_t> peers, std::shared_ptr<raft_storage_memory> storage);

	raft_node::node_state_e get_state() const;
	node_id_t get_id() const;

private:
	std::shared_ptr<raft_storage_memory> m_storage;
	std::shared_ptr<raft_state_machine> m_state_machine;
	raft_node m_node;
};

#endif
