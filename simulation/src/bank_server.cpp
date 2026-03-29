#include "simulation/bank_server.hpp"

#include <cassert>
#include <map>
#include <random>
#include <thread>

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
		std::unique_lock lock{m_mutex};
		assert(log_entry.command.size() == sizeof(bank_transaction));
		bank_transaction tx = *reinterpret_cast<const bank_transaction*>(log_entry.command.data());

		// When opening an account, the client gets some money out of thin air. So the sender is invalid.
		if (tx.from != INVALID_ACCOUNT_ID) {
			assert(m_balances[tx.from] >= tx.amount);
			m_balances[tx.from] -= tx.amount;
		}
		m_balances[tx.to] += tx.amount;
	}

	size_t get_balance(account_id_t account_id) const
	{
		std::unique_lock lock{m_mutex};
		return m_balances.at(account_id);
	}

private:
	mutable std::mutex m_mutex;
	// map account ID -> balance
	std::map<account_id_t, size_t> m_balances;
};

std::mutex bank_server::s_inbox_mutex{};
std::unordered_map<size_t, std::vector<raft_message_t>> bank_server::s_inbox{};

bank_server::bank_server(size_t id, std::vector<node_id_t> peers, std::shared_ptr<raft_storage> storage) :
	m_run(true),
	m_simulate_failures(true),
	m_id(id),
	m_peers(peers),
	m_storage(storage),
	m_state_machine(std::make_shared<bank_balances>()),
	m_node(std::make_unique<raft_node>(id, peers, storage, m_state_machine))
{
	std::lock_guard<std::mutex> lock{s_inbox_mutex};
	s_inbox.emplace(id, std::vector<raft_message_t>{});
}

raft_node::node_state_e
bank_server::get_state() const
{
	return m_node->get_state();
}

node_id_t
bank_server::get_id() const
{
	return m_node->get_id();
}

api_response_t
bank_server::open_account(account_id_t account_id)
{
	std::unique_lock<std::mutex> lock(m_mutex);

	bank_transaction tx{.from = INVALID_ACCOUNT_ID, .to = account_id, .amount = STARTING_BALANCE};
	const auto* bytes = reinterpret_cast<const uint8_t*>(&tx);
	log_entry_index_t log_index;

	if (!m_node) {
		goto return_error;
	}
	if (m_node->get_state() != raft_node::node_state_e::LEADER) {
		return {.type = api_response_type::REDIRECT, .redirect_to = m_node->get_leader_id()};
	}

	log_index = m_node->append_log({bytes, bytes + sizeof(tx)});
	server_tick.wait(lock, [&]() { return !m_node || m_node->get_commit_index() >= log_index; });
	if (!m_node) {
		goto return_error;
	}

	return {.type = api_response_type::SUCCESS, .redirect_to = INVALID_NODE_ID};
return_error:
	return {.type = api_response_type::ERROR, .redirect_to = INVALID_NODE_ID};
}

size_t
bank_server::get_balance(account_id_t account_id)
{
	std::unique_lock<std::mutex> lock(m_mutex);
	return std::static_pointer_cast<bank_balances>(m_state_machine)->get_balance(account_id);
}

api_response_t
bank_server::transfer(account_id_t from, account_id_t to, size_t amount)
{
	std::unique_lock<std::mutex> lock(m_mutex);

	size_t from_balance = std::static_pointer_cast<bank_balances>(m_state_machine)->get_balance(from);
	bank_transaction tx{.from = from, .to = to, .amount = amount};
	const auto* bytes = reinterpret_cast<const uint8_t*>(&tx);
	log_entry_index_t log_index;

	if (from_balance < amount) {
		goto return_error;
	}
	if (!m_node) {
		goto return_error;
	}
	if (m_node->get_state() != raft_node::node_state_e::LEADER) {
		return {.type = api_response_type::REDIRECT, .redirect_to = m_node->get_leader_id()};
	}

	log_index = m_node->append_log({bytes, bytes + sizeof(tx)});
	server_tick.wait(lock, [&]() { return !m_node || m_node->get_commit_index() >= log_index; });
	if (!m_node) {
		goto return_error;
	}

	return {.type = api_response_type::SUCCESS, .redirect_to = INVALID_NODE_ID};
return_error:
	return {.type = api_response_type::ERROR, .redirect_to = INVALID_NODE_ID};
}

void
bank_server::send_message(const raft_message_t& msg)
{
	std::lock_guard<std::mutex> lock{s_inbox_mutex};
	s_inbox.at(get_dest(msg)).push_back(msg);
}

std::vector<raft_message_t>
bank_server::get_messages(size_t id)
{
	std::lock_guard<std::mutex> lock{s_inbox_mutex};
	std::vector<raft_message_t> recv{};
	std::swap(recv, s_inbox.at(id));
	return recv;
}

void
bank_server::drive_node()
{
	while (m_run) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
		std::unique_lock<std::mutex> lock{m_mutex};
		std::vector<raft_message_t> messages = get_messages(m_node->get_id());
		m_node->step(messages);
		m_node->tick();
		messages = m_node->get_messages();
		for (const raft_message_t& msg : messages) {
			send_message(msg);
		}

		server_tick.notify_all();

		static thread_local std::random_device rd;
		static thread_local std::mt19937_64 rng{rd()};
		static thread_local std::uniform_int_distribution<uint64_t> un{0, 10000};
		// if 0 is rolled, shut down the server
		if (!un(rng) && m_simulate_failures) {
			m_node.reset();
			m_state_machine.reset();
			lock.unlock();
			std::this_thread::sleep_for(std::chrono::seconds(10));
			lock.lock();
			m_state_machine = std::make_shared<bank_balances>();
			m_node = std::make_unique<raft_node>(m_id, m_peers, m_storage, m_state_machine);
			// discard any received messages
			get_messages(m_id);
		}
	}
}
