#ifndef __BANK_SERVER_HPP__
#define __BANK_SERVER_HPP__

#include "raft/raft_node.hpp"
#include "raft/raft_state_machine.hpp"
#include "raft/raft_storage.hpp"
#include "simulation/api_response.hpp"

#include <condition_variable>
#include <memory>
#include <mutex>
#include <vector>

using account_id_t = size_t;
constexpr account_id_t INVALID_ACCOUNT_ID = static_cast<account_id_t>(-1);

class bank_server
{
public:
	bank_server(size_t id, std::vector<node_id_t> peers, std::shared_ptr<raft_storage> storage);

	raft_node::node_state_e get_state() const;

	node_id_t get_id() const;

	static constexpr size_t STARTING_BALANCE = 100000ul;
	api_response_t open_account(account_id_t account_id);

	api_response_t get_balance(account_id_t account_id, size_t& out_balance);

	api_response_t transfer(account_id_t from, account_id_t to, size_t amount);

	bool get_run() const
	{
		std::unique_lock<std::mutex> lock{m_mutex};
		return m_run;
	}

	void stop()
	{
		std::unique_lock<std::mutex> lock{m_mutex};
		m_run = false;
	};

	void stop_simulation_failures()
	{
		std::unique_lock<std::mutex> lock{m_mutex};
		m_simulate_failures = false;
	}

	void drive_node();

private:
	static void send_message(const raft_message_t& msg);
	static std::vector<raft_message_t> get_messages(size_t id);

	static std::mutex s_inbox_mutex;
	static std::unordered_map<size_t, std::vector<raft_message_t>> s_inbox;

	mutable std::mutex m_mutex;
	mutable std::condition_variable server_tick;
	bool m_run;
	bool m_simulate_failures;

	std::size_t m_id;
	std::vector<node_id_t> m_peers;
	std::shared_ptr<raft_storage> m_storage;
	std::shared_ptr<raft_state_machine> m_state_machine;
	std::unique_ptr<raft_node> m_node;
	log_entry_index_t m_commit_index;
};

#endif
