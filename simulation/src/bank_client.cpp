#include "simulation/bank_client.hpp"

#include <algorithm>
#include <cassert>

std::vector<const bank_client*> bank_client::s_clients;
account_id_t bank_client::s_next_account_id = 1ul;

bank_client::bank_client(std::span<std::shared_ptr<bank_server>> servers) :
	m_run(true),
	m_account_id(s_next_account_id++),
	m_servers(servers),
	m_leader_server(servers[0]),
	m_peers()
{
	assert(std::none_of(s_clients.begin(), s_clients.end(), [this](const bank_client* peer) {
		return this->m_account_id == peer->get_id();
	}));
	s_clients.push_back(this);
}

void
bank_client::drive_client()
{
	std::copy_if(s_clients.begin(), s_clients.end(), std::back_inserter(m_peers), [&](const bank_client* obj) {
		return obj != this;
	});

	// TODO: Register to a server with the account_id. Get the initial balance. In a loop begin transfering money to the
	// peers by using the servers. Notify the peer of transfer so peers update their balance independently of servers.
	// At the end check if the values match.

	while (m_run) {
	}
}
