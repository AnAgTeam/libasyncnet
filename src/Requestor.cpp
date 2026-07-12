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
		// Signal only: flip the flag and wake the worker so it observes it and tears
		// itself down (the worker self-detaches on loop exit). Thread disposition is
		// no longer handled here, so this is safe to call from any thread — the worker
		// itself, an external caller, or ~Requestor. NB: shutdown is now asynchronous;
		// it returns before the worker has necessarily stopped.
		if (shutting_down_.exchange(true, std::memory_order::acq_rel) == false) {
			wakeup_polling();
		}
	}

	bool Requestor::is_multithreaded() {
		return false;
	}

	std::shared_ptr<Requestor> Requestor::make_shared() {
		auto real = std::make_shared<Requestor>(private_constructor{});
		Requestor* raw = real.get();

		// The public handle is a SEPARATE control block over the same object. It does
		// not delete; its deleter (a) holds a strong ref for the whole time it runs —
		// so curl_multi_wakeup() can never touch a freed handle, and an explicit
		// shutdown() cannot destroy the object out from under a live handle — and
		// (b) signals shutdown when the LAST public handle drops. Shutdown thus becomes
		// an event, replacing the per-iteration use_count() poll.
		std::shared_ptr<Requestor> handle(raw, [owner = real](Requestor*) mutable noexcept {
			owner->shutting_down_.store(true, std::memory_order_release);
			owner->wakeup_polling();
			owner.reset();
		});

		// The worker owns the real (deleting) reference. Assign through `raw`: the LHS
		// receiver must not depend on `real`, which is moved into the thread capture.
		raw->yield_thread_ = std::thread([real = std::move(real)]() mutable {
			coro::sync_wait(yield_executor(std::move(real)));
		});
		return handle;
	}

	coro::task<void> Requestor::yield_executor(std::shared_ptr<Requestor> self) {
		// Shutdown is event-driven: the public handle's deleter sets the flag and wakes
		// the poll (curl_multi_wakeup). No use_count() poll and no heartbeat needed to
		// notice "no users left" — we block in yield() until an event arrives.
		while (!self->shutting_down_.load(std::memory_order_acquire)) {
			co_await self->yield(self->timeout_ms_);
		}

		// Always on the worker thread here, so joining ourselves is impossible: detach
		// up front so that whichever thread later runs ~Requestor finds yield_thread_
		// non-joinable (a joinable std::thread destructor calls std::terminate()).
		self->yield_thread_.detach();

		co_await self->cleanup();

		// ---- point of no return ----
		// Releasing the last reference runs ~Requestor HERE (or on the handle's last
		// dropper, whichever is last) — after this coroutine's frames have left the
		// stack and the thread is already detached. After this line *self is gone: no
		// member and no `this` is in scope. Keep this the LAST line.
		self.reset();
	}
}