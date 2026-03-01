#ifndef __RAFT_NODE_HPP__
#define __RAFT_NODE_HPP__

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

using node_id_t = std::size_t;
using leader_term_t = std::size_t;
using log_entry_index_t = std::size_t;

constexpr node_id_t INVALID_NODE_ID = 0;

class raft_node
{
public:
    enum class node_state_e
    {
        FOLLOWER,
        LEADER,
        CANDIDATE
    };

    raft_node(node_id_t id,
              std::vector<node_id_t> peers,
              size_t election_threshold,
              size_t heartbeat_threshold);

    node_id_t get_id() const { return m_id; }

    node_state_e get_state() const { return m_state; }

    void tick();

private:
    // Persistent state
    const node_id_t m_id;
    const std::vector<node_id_t> m_peers;
    node_state_e m_state;
    leader_term_t m_current_term;
    node_id_t m_voted_for;
    // TODO: log

    // Timeouts
    const size_t m_election_threshold;
    size_t m_election_timeout;
    const size_t m_heartbeat_threshold;
    size_t m_heartbeat_timeout;

    // Volatile state
    log_entry_index_t m_commit_index;
    log_entry_index_t m_last_applied;
    std::unordered_map<node_id_t, log_entry_index_t> m_next_index;
    std::unordered_map<node_id_t, log_entry_index_t> m_match_index;
};

#endif