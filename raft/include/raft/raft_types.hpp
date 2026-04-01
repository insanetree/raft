#ifndef __RAFT_TYPES_HPP__
#define __RAFT_TYPES_HPP__

#include <cstddef>
#include <cstdint>
#include <vector>

using node_id_t = std::size_t;
constexpr node_id_t INVALID_NODE_ID = 0;

using leader_term_t = std::size_t;

using log_entry_index_t = std::size_t;

struct log_entry_t
{
	leader_term_t term;
	std::vector<uint8_t> command;

	bool operator==(const log_entry_t& right) const
	{
		if (term != right.term) {
			return false;
		}
		if (command != right.command) {
			return false;
		}
		return true;
	}
};

#endif
