#ifndef __RAFT_NODE_HPP__
#define __RAFT_NODE_HPP__

#include <cstdint>
#include <utility>

using node_id_t = std::size_t;

class raft_node {
public:
    enum class node_state_e {
        FOLLOWER,
        LEADER,
        CANDIDATE
    };

private:
    node_id_t m_id;
    node_state_e m_state;
};

#endif