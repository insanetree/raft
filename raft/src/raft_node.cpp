#include "raft_node.hpp"
#include <vector>

raft_node::raft_node(node_id_t id,
                     std::vector<node_id_t> peers,
                     size_t election_threshold,
                     size_t heartbeat_threshold)
  : m_id(id)
  , m_peers(peers)
  , m_state(node_state_e::FOLLOWER)
  , m_current_term(0)
  , m_voted_for(INVALID_NODE_ID)
  , m_election_threshold(election_threshold)
  , m_heartbeat_threshold(heartbeat_threshold)
{
}

void
raft_node::tick()
{
}