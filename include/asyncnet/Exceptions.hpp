#pragma once
#include <exception>
#include <stdexcept>

namespace asyncnet {

	struct NetworkLogicError : std::logic_error {
		using std::logic_error::logic_error;
	};

	struct NetworkRuntimeError : std::runtime_error {
		using std::runtime_error::runtime_error;
	};
};