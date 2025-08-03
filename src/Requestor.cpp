#include <asyncnet/Requestor.hpp>

#include <curlpp/Options.hpp>
#include <sstream>

constexpr int curl_cancel_request = 1;
constexpr int curl_continue_request = 0;

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

	CancellingTask<Response> Requestor::perform_handle(curlpp::Easy handle) const throw() {
		co_await pool_->schedule();

		handle.setOpt(
			curlpp::options::ProgressFunction([stop_token = co_await awaitables::get_stop_token](double, double, double, double) -> int {
				return stop_token.stop_requested() ? curl_cancel_request : curl_continue_request;
			})
		);
		handle.setOpt(curlpp::options::NoProgress(false));

		std::ostringstream stream;
		handle.setOpt(curlpp::options::WriteStream(&stream));

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
			co_return Response(std::move(handle), std::move(stream));
		}

		// handle throwed exception
		std::rethrow_exception(exception);
	}

	CancellingTask<Response> Requestor::perform_request(const Request& request) const throw() {
		return perform_handle(request.make_request_handle());
	}

};
