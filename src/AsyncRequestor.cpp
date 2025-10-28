#include <asyncnet/AsyncRequestor.hpp>
#include <asyncnet/NetTypes.hpp>
#include <asyncnet/Exceptions.hpp>

#include <curlpp/cURLpp.hpp>
#include <coro/sync_wait.hpp>

#include <iostream>

namespace asyncnet {
	AsyncRequestor::AsyncRequestor(private_constructor) :
		timeout_ms_(1000),
		shutting_down_(false)
	{

	}

	AsyncRequestor::~AsyncRequestor() {
		shutdown();
	}

	CancellingTask<Response> AsyncRequestor::perform_handle(curlpp::Easy handle) {
		if (shutting_down_.load(std::memory_order_acquire)) {
			throw RuntimeError("Cannot perform_handle when AsyncRequestor is shutting down");
		}
		co_return co_await CurlMulti::perform_handle(std::move(handle)).update_stop_source(co_await awaitables::get_stop_source);
	}

	CancellingTask<Response> AsyncRequestor::perform_request(const Request& request) {
		return perform_handle(request.make_request_handle());
	}

	void AsyncRequestor::shutdown() {
		if (shutting_down_.exchange(true, std::memory_order_acq_rel) == false) {
			if (std::this_thread::get_id() != yield_thread_.get_id()) {
				wakeup_polling();
				yield_thread_.join();
			}
			else {
				// detach thread, because trying to shutdown within 'yield_thread_'
				yield_thread_.detach();
			}
		}
	}

	std::shared_ptr<AsyncRequestor> AsyncRequestor::make_shared() {
		auto ptr = std::make_shared<AsyncRequestor>(private_constructor{});
		ptr->yield_thread_ = std::thread([ptr = ptr.get()] { coro::sync_wait(ptr->yield_executor()); });
		return ptr;
	}

	coro::task<void> AsyncRequestor::yield_executor() {
		auto p = shared_from_this();
		while (!shutting_down_.load(std::memory_order_acquire)) {
			if (p.use_count() == 1) {
				// there is only executor referencing the object, so shutdown
				// TODO: use coroutine features to shutdown
				shutdown();
			}
			co_await yield(timeout_ms_);
		}
		co_await cleanup();
	}
}