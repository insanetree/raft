#include "simulation/raft_server.hpp"

raft_server::raft_server(node_id_t id, std::vector<node_id_t> peers) :
	m_storage(std::make_shared<raft_storage_memory>()),
	m_node(id, std::move(peers), m_storage)
{
}

raft_node::node_state_e
raft_server::get_state() const
{
	return m_node.get_state();
}

node_id_t
raft_server::get_id() const
{
	return m_node.get_id();
}
