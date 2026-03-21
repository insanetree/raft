#include "simulation/bank_server.hpp"

#include <cstdio>

int
main()
{
	std::shared_ptr<raft_storage_memory> storage = std::make_shared<raft_storage_memory>();
	bank_server server(1, {2, 3}, storage);
	std::printf("Server %zu started in state %d\n", server.get_id(), static_cast<int>(server.get_state()));
	return 0;
}
