#include "raft/raft_storage_memory.hpp"
#include "simulation/bank_client.hpp"
#include "simulation/bank_server.hpp"
#include "spdlog/spdlog.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <latch>
#include <thread>
#include <vector>

constexpr size_t CLUSTER_SIZE = 5;
constexpr size_t CLIENT_NUM = 10;
std::array<std::shared_ptr<raft_storage>, CLUSTER_SIZE> g_storage_array{};
std::array<std::shared_ptr<bank_server>, CLUSTER_SIZE> g_server_array{};
std::array<std::shared_ptr<bank_client>, CLIENT_NUM> g_client_array{};

int
main()
{
	spdlog::set_level(spdlog::level::debug);
	spdlog::info("Starting simulation");
	for (size_t i = 0; i < CLUSTER_SIZE; i++) {
		g_storage_array[i] = std::make_shared<raft_storage_memory>();
		std::vector<node_id_t> peers;
		for (size_t j = 0; j < CLUSTER_SIZE; j++) {
			if (i != j) {
				peers.push_back(j + 1);
			}
		}
		g_server_array[i] = std::make_shared<bank_server>(i + 1, peers, g_storage_array[i]);
	}

	for (size_t i = 0; i < CLIENT_NUM; i++) {
		g_client_array[i] = std::make_shared<bank_client>(std::span(g_server_array));
	}

	std::vector<std::jthread> threads_array;
	std::barrier<> client_barrier{CLIENT_NUM + 1};
	std::latch server_latch{CLUSTER_SIZE};
	std::for_each(g_server_array.begin(),
	              g_server_array.end(),
	              [&threads_array, &server_latch](std::shared_ptr<bank_server> server) {
					  threads_array.emplace_back([server, &server_latch]() { server->drive_node(server_latch); });
				  });
	std::this_thread::sleep_for(std::chrono::seconds(1));
	std::for_each(g_client_array.begin(),
	              g_client_array.end(),
	              [&threads_array, &client_barrier](std::shared_ptr<bank_client> client) {
					  threads_array.emplace_back([client, &client_barrier]() { client->drive_client(client_barrier); });
				  });
	client_barrier.arrive_and_wait();

	// let clients run for a bit
	std::this_thread::sleep_for(std::chrono::seconds(10));

	// stop server failures
	spdlog::info("Stopping server failure simulation");
	std::for_each(g_server_array.begin(), g_server_array.end(), [](std::shared_ptr<bank_server> server) {
		server->stop_simulation_failures();
	});

	// stop clients
	std::for_each(
		g_client_array.begin(), g_client_array.end(), [](std::shared_ptr<bank_client> client) { client->stop(); });

	// wait for clients to stop transactions
	client_barrier.arrive_and_wait();

	// stop servers
	std::for_each(
		g_server_array.begin(), g_server_array.end(), [](std::shared_ptr<bank_server> server) { server->stop(); });
	server_latch.wait();

	// wait for clients and servers to stop
	std::for_each(threads_array.begin(), threads_array.end(), [](std::jthread& thread) { thread.join(); });

	assert(std::all_of(g_storage_array.begin(), g_storage_array.end(), [](std::shared_ptr<raft_storage> storage) {
		return *storage == *g_storage_array[0];
	}));

	size_t sum = std::transform_reduce(g_client_array.begin(),
	                                   g_client_array.end(),
	                                   0ul,
	                                   std::plus<>(),
	                                   [](const std::shared_ptr<bank_client> client) { return client->get_balance(); });
	if (sum != CLIENT_NUM * bank_server::STARTING_BALANCE) {
		spdlog::critical("FAIL: sum={}, staring sum={}", sum, CLIENT_NUM * bank_server::STARTING_BALANCE);
		std::abort();
	}

	return 0;
}
