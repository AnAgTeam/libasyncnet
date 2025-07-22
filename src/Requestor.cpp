#include <asyncnet/Requestor.hpp>

namespace asyncnet {

	Requestor::Requestor(const unsigned worker_count) :
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

	Requestor::Requestor(const unsigned worker_count, std::shared_ptr<coro::thread_pool> executor_pool) :
		pool_(coro::thread_pool::make_shared(
			coro::thread_pool::options{
				.thread_count = worker_count
			}
		)),
		after_pool_(executor_pool)
	{

	}

	coro::task<void> Requestor::perform_handle(curlpp::Easy& handle) const {
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
		std::rethrow_exception(exception);
	}

	coro::task<void> Requestor::perform_request(std::shared_ptr<Request> request) const {
		co_await perform_handle(request->handle_);
	}

};
