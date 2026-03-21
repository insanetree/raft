#include "simulation/bank_server.hpp"
#include <cassert>
#include <map>

class bank_balances : public raft_state_machine
{
public:
	void apply(log_entry_t log_entry) override
	{
		assert(log_entry.command.size() == sizeof(bank_transaction));
		bank_transaction tx = *reinterpret_cast<const bank_transaction*>(log_entry.command.data());
		assert(m_balances[tx.from] >= tx.amount);
		m_balances[tx.from] -= tx.amount;
		m_balances[tx.to] += tx.amount;
	}

private:
	// map account ID -> balance
	std::map<size_t, size_t> m_balances;
};

bank_server::bank_server(node_id_t id, std::vector<node_id_t> peers, std::shared_ptr<raft_storage_memory> storage) :
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
