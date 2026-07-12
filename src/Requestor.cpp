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
		if (shutting_down_.load(std::memory_order::acquire)) {
			throw RuntimeError("Cannot perform_request when AsyncRequestor is shutting down");
		}
		co_return co_await CurlMulti::perform_handle(request.make_request_handle(), request.get_output_stream());
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
		ptr->yield_thread_ = std::thread([self = ptr]() mutable {
			// The coroutine frame is owned by sync_wait, NOT by *self, so destroying
			// *self from inside it is safe. yield_executor is a free (static) call, so
			// there is no receiver to evaluate against the moved-from pointer.
			coro::sync_wait(yield_executor(std::move(self)));
		});
		return ptr;
	}

	coro::task<void> Requestor::yield_executor(std::shared_ptr<Requestor> self) {
		while (!self->shutting_down_.load(std::memory_order_acquire)) {
			if (self.use_count() == 1) {
				// there is only executor referencing the object, so shutdown
				// TODO: use coroutine features to shutdown
				self->shutdown();
			}
			co_await self->yield(self->timeout_ms_);
		}
		co_await self->cleanup();

		// ---- point of no return ----
		// Releasing the last reference runs ~Requestor HERE, on this (now detached)
		// thread, only after all of this coroutine's own frames have left the stack.
		// After this line `self` is empty and *self is gone: no member and no `this`
		// is in scope, so nothing below can touch dead state. Keep this the LAST line.
		self.reset();
	}
}