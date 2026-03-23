#ifndef __BANK_SERVER_HPP__
#define __BANK_SERVER_HPP__

#include "raft/raft_node.hpp"
#include "raft/raft_state_machine.hpp"
#include "raft/raft_storage_memory.hpp"
#include "simulation/api_response.hpp"

#include <memory>
#include <mutex>
#include <vector>

using account_id_t = size_t;
constexpr account_id_t INVALID_ACCOUNT_ID = static_cast<account_id_t>(-1);

class bank_server
{
public:
	bank_server(node_id_t id, std::vector<node_id_t> peers, std::shared_ptr<raft_storage> storage);

	raft_node::node_state_e get_state() const;

	node_id_t get_id() const;

	api_response_t open_account(account_id_t account_id);

	api_response_t transfer(account_id_t from, account_id_t to, size_t amount);

private:
	std::recursive_mutex m_mutex;

	std::shared_ptr<raft_storage> m_storage;
	std::shared_ptr<raft_state_machine> m_state_machine;
	raft_node m_node;
};

#endif
