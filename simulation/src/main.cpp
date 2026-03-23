#include "simulation/bank_server.hpp"

#include <cstdio>
#include <vector>

constexpr size_t CLUSTER_SIZE = 5;
std::vector<std::shared_ptr<raft_storage>> g_storage_array(CLUSTER_SIZE, nullptr);
std::vector<std::shared_ptr<bank_server>> g_server_array(CLUSTER_SIZE, nullptr);
std::vector<raft_message_t> g_message_queue;

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
	std::printf("Server %zu started in state %d\n",
	            g_server_array[0]->get_id(),
	            static_cast<int>(g_server_array[0]->get_state()));
	return 0;
}
