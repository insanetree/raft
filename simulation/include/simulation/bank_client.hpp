#ifndef __BANK_CLIENT_HPP__
#define __BANK_CLIENT_HPP__

#include "simulation/bank_server.hpp"

#include <span>

class bank_client
{
public:
	bank_client(std::span<std::shared_ptr<bank_server>> servers);
	bank_client(const bank_client&) = delete;
	bank_client(bank_client&&) = delete;

	account_id_t get_id() const
	{
		std::unique_lock<std::mutex> lock{m_mutex};
		return m_account_id;
	}

	void drive_client();

	void stop()
	{
		std::unique_lock<std::mutex> lock{m_mutex};
		m_run = false;
	};

private:
	static std::vector<const bank_client*> s_clients;
	static account_id_t s_next_account_id;

	mutable std::mutex m_mutex;

	bool m_run;
	account_id_t m_account_id;
	size_t m_balance;

	std::span<std::shared_ptr<bank_server>> m_servers;
	std::shared_ptr<bank_server> m_leader_server;
	std::vector<const bank_client*> m_peers;
};

#endif
