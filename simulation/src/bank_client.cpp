#include "simulation/bank_client.hpp"
#include "spdlog/spdlog.h"

#include <algorithm>
#include <cassert>
#include <thread>

std::vector<bank_client*> bank_client::s_clients;
account_id_t bank_client::s_next_account_id = 1ul;

bank_client::bank_client(std::span<std::shared_ptr<bank_server>> servers) :
	m_rd(),
	m_rng(m_rd()),
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
bank_client::transfer(size_t amount)
{
	std::unique_lock<std::mutex> lock{m_mutex};
	m_balance += amount;
}

void
bank_client::random_transfer()
{
	std::unique_lock<std::mutex> lock{m_mutex};
	if (m_balance == 0) {
		return;
	}
	std::uniform_int_distribution<size_t> dist(1, m_balance);
	size_t amount = dist(m_rng);
	bank_client* peer = m_peers[m_rng() % m_peers.size()];
	m_balance -= amount;
	lock.unlock(); // need to unlock this to avoid deadlock when peer transfers
	peer->transfer(amount);

	api_response_t resp;
	do {
		resp = m_leader_server->transfer(m_account_id, peer->get_id(), amount);
		if (resp.type != api_response_type::SUCCESS) {
			if (resp.redirect_to != INVALID_NODE_ID) {
				m_leader_server = m_servers[resp.redirect_to - 1];
			} else {
				m_leader_server = m_servers[m_rng() % m_servers.size()];
			}
		}
	} while (resp.type != api_response_type::SUCCESS);
}

void
bank_client::drive_client()
{
	spdlog::info("CLIENT {}: online", m_account_id);
	std::copy_if(
		s_clients.begin(), s_clients.end(), std::back_inserter(m_peers), [&](bank_client* obj) { return obj != this; });

	// TODO: Register to a server with the account_id. Get the initial balance. In a loop begin transfering money to the
	// peers by using the servers. Notify the peer of transfer so peers update their balance independently of servers.
	// At the end check if the values match.
	api_response_t resp;
	size_t balance;
	do {
		resp = m_leader_server->open_account(m_account_id);
		if (resp.type != api_response_type::SUCCESS) {
			if (resp.redirect_to != INVALID_NODE_ID) {
				m_leader_server = m_servers[resp.redirect_to - 1];
			} else {
				m_leader_server = m_servers[m_rng() % m_servers.size()];
			}
		}
	} while (resp.type != api_response_type::SUCCESS);
	do {
		resp = m_leader_server->get_balance(m_account_id, balance);
		if (resp.type != api_response_type::SUCCESS) {
			if (resp.redirect_to != INVALID_NODE_ID) {
				m_leader_server = m_servers[resp.redirect_to - 1];
			} else {
				m_leader_server = m_servers[m_rng() % m_servers.size()];
			}
		}
	} while (resp.type != api_response_type::SUCCESS);
	m_balance = balance;

	while (m_run) {
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
		random_transfer();
	}
	spdlog::info("CLIENT {}: stopping", m_account_id);

	// wait for servers to stabilize
	std::this_thread::sleep_for(std::chrono::seconds(5));

	do {
		resp = m_leader_server->get_balance(m_account_id, balance);
		if (resp.type != api_response_type::SUCCESS) {
			if (resp.redirect_to != INVALID_NODE_ID) {
				m_leader_server = m_servers[resp.redirect_to - 1];
			} else {
				m_leader_server = m_servers[m_rng() % m_servers.size()];
			}
		}
	} while (resp.type != api_response_type::SUCCESS);
	assert(balance == m_balance);
}
