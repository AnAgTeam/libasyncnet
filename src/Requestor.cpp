#include <asyncnet/Requestor.hpp>
#include <asyncnet/NetTypes.hpp>
#include <asyncnet/Exceptions.hpp>

#include <curlpp/cURLpp.hpp>
#include <coro/sync_wait.hpp>

#include <iostream>

namespace asyncnet {
	Requestor::Requestor(private_constructor) :
		timeout_ms_(1000),
		shutting_down_(false)
	{

	}

	Requestor::~Requestor() {
		shutdown();
	}

	NetworkTask<Response> Requestor::perform_handle(curlpp::Easy handle) {
		if (shutting_down_.load(std::memory_order::acquire)) {
			throw RuntimeError("Cannot perform_handle when AsyncRequestor is shutting down");
		}
		co_return co_await CurlMulti::perform_handle(std::move(handle));
	}

	NetworkTask<Response> Requestor::perform_request(const Request& request) {
		return perform_handle(request.make_request_handle());
	}

	void Requestor::shutdown() {
		if (shutting_down_.exchange(true, std::memory_order::acq_rel) == false) {
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

	bool Requestor::is_multithreaded() {
		return false;
	}

	std::shared_ptr<Requestor> Requestor::make_shared() {
		auto ptr = std::make_shared<Requestor>(private_constructor{});
		ptr->yield_thread_ = std::thread([ptr = ptr.get()] { coro::sync_wait(ptr->yield_executor()); });
		return ptr;
	}

	coro::task<void> Requestor::yield_executor() {
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