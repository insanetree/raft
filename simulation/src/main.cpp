#include "raft/raft_storage_memory.hpp"
#include "simulation/bank_client.hpp"
#include "simulation/bank_server.hpp"

#include <array>
#include <cstdio>
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
	std::for_each(g_server_array.begin(), g_server_array.end(), [&threads_array](std::shared_ptr<bank_server> server) {
		threads_array.emplace_back([server]() { server->drive_node(); });
	});
	std::for_each(g_client_array.begin(), g_client_array.end(), [&threads_array](std::shared_ptr<bank_client> client) {
		threads_array.emplace_back([client]() { client->drive_client(); });
	});
	return 0;
}
