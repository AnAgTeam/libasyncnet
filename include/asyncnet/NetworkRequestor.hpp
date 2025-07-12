#pragma once
#include <asyncnet/Exceptions.hpp>
#include <asyncnet/ThreadsafeQueues.hpp>
#include <curlpp/Easy.hpp>
#include <coro/thread_pool.hpp>

namespace asyncnet {

	struct NetworkRequestError : NetworkRuntimeError {
		explicit NetworkRequestError();
	};

	class NetworkRequestor {
	public:
		explicit NetworkRequestor(const unsigned worker_count);
		explicit NetworkRequestor(const unsigned worker_count, std::shared_ptr<coro::thread_pool> executor_pool);
		NetworkRequestor(const NetworkRequestor& other) = default;
		NetworkRequestor(NetworkRequestor&& other) = default;
		~NetworkRequestor() = default;

		coro::task<void> perform_handle(curlpp::Easy& handle) const;

	private:

		std::shared_ptr<coro::thread_pool> pool_;
		std::shared_ptr<coro::thread_pool> after_pool_;
	};
}