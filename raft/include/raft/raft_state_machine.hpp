#ifndef __RAFT_STATE_MACHINE_HPP__
#define __RAFT_STATE_MACHINE_HPP__

#include "raft/raft_types.hpp"

class raft_state_machine
{
public:
	virtual ~raft_state_machine() = default;

	virtual void apply(log_entry_t log_entry) = 0;

protected:
	raft_state_machine() = default;
};

#endif
