#include <asyncnet/NetworkRequestor.hpp>

namespace asyncnet {

	NetworkRequestError::NetworkRequestError() : NetworkRuntimeError("Request failed") {

	}

	NetworkRequestor::NetworkRequestor(const unsigned worker_count) :
		pool_(coro::thread_pool::make_shared(
			coro::thread_pool::options {
				.thread_count = worker_count
			}
		)),
		after_pool_(coro::thread_pool::make_shared(
			coro::thread_pool::options {
				.thread_count = 1
			}
		))
	{

	}

	NetworkRequestor::NetworkRequestor(const unsigned worker_count, std::shared_ptr<coro::thread_pool> executor_pool) :
		pool_(coro::thread_pool::make_shared(
			coro::thread_pool::options{
				.thread_count = worker_count
			}
		)),
		after_pool_(executor_pool)
	{

	}

	coro::task<void> NetworkRequestor::perform_handle(curlpp::Easy& handle) const {
		co_await pool_->schedule();

		std::exception_ptr exception;
		try {
			handle.perform();
		}
		catch (...) {
			exception = std::current_exception();
		}

		// user can pass custom pool with nullptr
		if (after_pool_) {
			co_await after_pool_->schedule();
		}

		if (!exception) {
			co_return;
		}

		// handle throwed exception
		try {
			std::rethrow_exception(exception);
		}
		catch (const curlpp::LogicError& e) {
			std::throw_with_nested(NetworkRequestError());
		}
		catch (const curlpp::RuntimeError& e) {
			std::throw_with_nested(NetworkRequestError());
		}
		catch (...) {
			std::rethrow_exception(exception);
		}
	}

};
