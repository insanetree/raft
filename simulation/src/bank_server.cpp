#include "simulation/bank_server.hpp"

#include <cassert>
#include <map>

struct bank_transaction
{
	account_id_t from;
	account_id_t to;
	size_t amount;
};

class bank_balances : public raft_state_machine
{
public:
	void apply(log_entry_t log_entry) override
	{
		assert(log_entry.command.size() == sizeof(bank_transaction));
		bank_transaction tx = *reinterpret_cast<const bank_transaction*>(log_entry.command.data());

		// When opening an account, the client gets some money out of thin air. So the sender is invalid.
		if (tx.from != INVALID_ACCOUNT_ID) {
			assert(m_balances[tx.from] >= tx.amount);
			m_balances[tx.from] -= tx.amount;
		}
		m_balances[tx.to] += tx.amount;
	}

	size_t get_balance(account_id_t account_id) const { return m_balances.at(account_id); }

private:
	// map account ID -> balance
	std::map<account_id_t, size_t> m_balances;
};

bank_server::bank_server(node_id_t id, std::vector<node_id_t> peers, std::shared_ptr<raft_storage> storage) :
	m_storage(storage),
	m_state_machine(std::make_shared<bank_balances>()),
	m_node(id, std::move(peers), m_storage, m_state_machine)
{
}

raft_node::node_state_e
bank_server::get_state() const
{
	return m_node.get_state();
}

node_id_t
bank_server::get_id() const
{
	return m_node.get_id();
}

api_response_t
bank_server::open_account(account_id_t account_id)
{
	std::lock_guard<std::recursive_mutex> lock(m_mutex);

	if (m_node.get_state() != raft_node::node_state_e::LEADER) {
		return {.type = api_response_type::REDIRECT, .redirect_to = m_node.get_leader_id()};
	}

	bank_transaction tx{.from = INVALID_ACCOUNT_ID, .to = account_id, .amount = 1000000ul};
	const auto* bytes = reinterpret_cast<const uint8_t*>(&tx);
	log_entry_t entry{.term = m_node.get_term(), .command = {bytes, bytes + sizeof(tx)}};
	m_storage->push_log_entry(entry);

	return {.type = api_response_type::SUCCESS, .redirect_to = INVALID_NODE_ID};
}

api_response_t
bank_server::transfer(account_id_t from, account_id_t to, size_t amount)
{
	std::lock_guard<std::recursive_mutex> lock(m_mutex);

	if (m_node.get_state() != raft_node::node_state_e::LEADER) {
		return {.type = api_response_type::REDIRECT, .redirect_to = m_node.get_leader_id()};
	}

	size_t from_balance = std::static_pointer_cast<bank_balances>(m_state_machine)->get_balance(from);
	if (from_balance < amount) {
		return {.type = api_response_type::ERROR, .redirect_to = INVALID_NODE_ID};
	}

	bank_transaction tx{.from = from, .to = to, .amount = amount};
	const auto* bytes = reinterpret_cast<const uint8_t*>(&tx);
	log_entry_t entry{.term = m_node.get_term(), .command = {bytes, bytes + sizeof(tx)}};
	m_storage->push_log_entry(entry);

	return {.type = api_response_type::SUCCESS, .redirect_to = INVALID_NODE_ID};
}
