#ifndef __BANK_CLIENT_HPP__
#define __BANK_CLIENT_HPP__

#include "simulation/bank_server.hpp"

#include <random>
#include <span>

class bank_client
{
public:
	bank_client(std::span<std::shared_ptr<bank_server>> servers);
	bank_client(const bank_client&) = delete;
	bank_client(bank_client&&) = delete;
	~bank_client();

	account_id_t get_id() const { return m_account_id; }

	void drive_client();

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

	void transfer(size_t amount);

private:
	void random_transfer();

	static std::vector<bank_client*> s_clients;
	static account_id_t s_next_account_id;

	mutable std::mutex m_mutex;
	std::random_device m_rd;
	std::mt19937_64 m_rng;

	bool m_run;
	account_id_t m_account_id;
	size_t m_balance;
	size_t m_completed_transfers;

	std::span<std::shared_ptr<bank_server>> m_servers;
	std::shared_ptr<bank_server> m_leader_server;
	std::vector<bank_client*> m_peers;
};

#endif
