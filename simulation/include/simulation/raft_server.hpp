#ifndef __RAFT_SERVER_HPP__
#define __RAFT_SERVER_HPP__

#include "raft/raft_node.hpp"
#include "raft/raft_storage_memory.hpp"

#include <memory>
#include <vector>

class raft_server
{
public:
	raft_server(node_id_t id, std::vector<node_id_t> peers, std::shared_ptr<raft_storage_memory> storage);

	raft_node::node_state_e get_state() const;
	node_id_t get_id() const;

private:
	std::shared_ptr<raft_storage_memory> m_storage;
	raft_node m_node;
};

#endif
