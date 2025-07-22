#pragma once
#include <asyncnet/Exceptions.hpp>
#include <asyncnet/Request.hpp>
#include <asyncnet/Response.hpp>

#include <curlpp/Easy.hpp>
#include <coro/thread_pool.hpp>
// #include <expected>

namespace asyncnet {

	class Requestor {
	public:
		/**
		 * Constructs with thread pool with worker_count size. After request user code executed in another special thread
		 * @param worker_count Threads count to execute in parallel for requests
		 */
		explicit Requestor(const unsigned worker_count);

		/**
		 * Constructs with thread pool with worker_count size. After request user code executed on executor_pool thread
		 * @param worker_count Threads count to execute in parallel for requests
		 * @param executor_pool The pool to execute after performing request
		 */
		explicit Requestor(const unsigned worker_count, std::shared_ptr<coro::thread_pool> executor_pool);

		Requestor(const Requestor& other) = default;
		Requestor(Requestor&& other) = default;
		~Requestor() = default;

		/**
		 * Switches to Requestor's thread, perfroms request, and then switches to special or executor_pool thread depending on construction @ref Requestor::Requestor.
		 * If timedout the @ref NetworkRuntimeError code will be @ref TimeoutErrorCode, if cancelled the code will be @ref CancelledErrorCode
		 * @param handle The handle to execute asyncronously 
		 * @return Retuns awaitable task
		 * @throws NetworkRuntimeError If any runtime error
		 * @throws NetworkLogicError If any logic error
		 */
		coro::task<void> perform_handle(curlpp::Easy& handle) const throw();

		/** @copydoc perform_handle(handle)
		 * Grabs handle from request and performs it
		 */
		coro::task<Response> perform_request(std::shared_ptr<Request> request) const throw();

	private:

		std::shared_ptr<coro::thread_pool> pool_;
		std::shared_ptr<coro::thread_pool> after_pool_;
	};
}