#ifndef __API_RESPONSE_HPP__
#define __API_RESPONSE_HPP__

#include "raft/raft_types.hpp"

enum class api_response_type
{
	SUCCESS,
	ERROR,
	REDIRECT,
	AGAIN
};

struct api_response_t
{
	api_response_type type;
	node_id_t redirect_to;
};

#endif // __API_RESPONSE_HPP__
