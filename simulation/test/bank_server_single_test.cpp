#include <gtest/gtest.h>

#include "raft/raft_storage_memory.hpp"
#include "simulation/bank_server.hpp"

class BankServerSingleTest : public ::testing::Test
{
protected:
	void SetUp() override
	{
		m_storage = std::make_shared<raft_storage_memory>();
		m_server = std::make_shared<bank_server>(1, std::vector<node_id_t>{}, m_storage);
		m_server->stop_simulation_failures();
	}

	void TearDown() override
	{
		m_server->stop();
		if (m_driver.joinable()) {
			m_driver.join();
		}
	}

	void drive()
	{
		m_driver = std::thread([&]() { m_server->drive_node(); });
	}

	std::shared_ptr<raft_storage_memory> m_storage;
	std::shared_ptr<bank_server> m_server;
	std::thread m_driver;
};

TEST_F(BankServerSingleTest, InitialStateIsFollower)
{
	EXPECT_EQ(m_server->get_state(), raft_node::node_state_e::FOLLOWER);
}

TEST_F(BankServerSingleTest, TransitionToLeader)
{
	EXPECT_EQ(m_server->get_state(), raft_node::node_state_e::FOLLOWER);
	drive();
	std::this_thread::sleep_for(std::chrono::seconds(2));
	EXPECT_EQ(m_server->get_state(), raft_node::node_state_e::LEADER);
}

TEST_F(BankServerSingleTest, OpenAccount)
{
	drive();
	std::this_thread::sleep_for(std::chrono::seconds(2));
	EXPECT_EQ(m_server->get_state(), raft_node::node_state_e::LEADER);

	api_response_t resp;
	account_id_t client_id = 1;
	resp = m_server->open_account(client_id);
	ASSERT_EQ(resp.type, api_response_type::SUCCESS);

	size_t client_balance = 0;
	resp = m_server->get_balance(client_id, client_balance);
	ASSERT_EQ(resp.type, api_response_type::SUCCESS);
	ASSERT_EQ(client_balance, bank_server::STARTING_BALANCE);
}

TEST_F(BankServerSingleTest, TransferFunds)
{
	drive();
	std::this_thread::sleep_for(std::chrono::seconds(2));
	EXPECT_EQ(m_server->get_state(), raft_node::node_state_e::LEADER);

	api_response_t resp;
	account_id_t from = 1;
	account_id_t to = 2;
	size_t from_balance;
	size_t to_balance;
	resp = m_server->open_account(from);
	ASSERT_EQ(resp.type, api_response_type::SUCCESS);
	resp = m_server->open_account(to);
	ASSERT_EQ(resp.type, api_response_type::SUCCESS);
	resp = m_server->transfer(from, to, 10ul);
	ASSERT_EQ(resp.type, api_response_type::SUCCESS);
	resp = m_server->get_balance(from, from_balance);
	ASSERT_EQ(resp.type, api_response_type::SUCCESS);
	resp = m_server->get_balance(to, to_balance);
	ASSERT_EQ(resp.type, api_response_type::SUCCESS);
	ASSERT_EQ(from_balance, bank_server::STARTING_BALANCE - 10ul);
	ASSERT_EQ(to_balance, bank_server::STARTING_BALANCE + 10ul);
}
