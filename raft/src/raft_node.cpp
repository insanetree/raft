#include "raft_node.hpp"

#include <vector>

raft_node::raft_node(node_id_t id,
                     std::vector<node_id_t> peers,
                     size_t election_threshold,
                     size_t heartbeat_threshold,
                     std::shared_ptr<raft_storage> storage) :
	m_id(id),
	m_peers(peers),
	m_state(node_state_e::FOLLOWER),
	m_storage(storage),
	m_election_threshold(election_threshold),
	m_election_timeout(0),
	m_heartbeat_threshold(heartbeat_threshold),
	m_heartbeat_timeout(0)
{
}

void
raft_node::tick()
{
	switch (m_state) {
	case node_state_e::FOLLOWER:
		m_election_timeout++;
		break;
	case node_state_e::LEADER:
		m_heartbeat_timeout++;
		break;
	case node_state_e::CANDIDATE:
		m_election_timeout++;
		break;
	}
}