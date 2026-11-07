#include <asyncnet/Requestor.hpp>
#include <asyncnet/Exceptions.hpp>

namespace asyncnet {
	Requestor::Requestor(private_constructor) : shutting_down_(false) {}

	Requestor::~Requestor() {
		shutdown();
		// The worker holds the final owner during automatic shutdown. It has already
		// finished cleanup when that owner is released; only its thread needs detaching.
		if (std::this_thread::get_id() == worker_id_ && yield_thread_.joinable())
			yield_thread_.detach();
	}

	NetworkTask<Response> Requestor::perform_handle(curlpp::Easy handle) {
		rethrow_failure();
		if (shutting_down_.load(std::memory_order::acquire)) {
			rethrow_failure();
			throw RuntimeError("Cannot perform_handle when AsyncRequestor is shutting down");
		}
		co_return co_await CurlMulti::perform_handle(std::move(handle));
	}

	NetworkTask<Response> Requestor::perform_request(const Request& request) {
		rethrow_failure();
		if (shutting_down_.load(std::memory_order::acquire)) {
			rethrow_failure();
			throw RuntimeError("Cannot perform_request when AsyncRequestor is shutting down");
		}
		co_return co_await CurlMulti::perform_handle(request.make_request_handle(), request.get_output_stream());
	}

	void Requestor::shutdown() {
		shutting_down_.store(true, std::memory_order_release);
		wakeup_polling();
		// A continuation may call shutdown on the worker. Joining itself would
		// deadlock; the synchronous loop will observe the flag after resume returns.
		if (std::this_thread::get_id() == worker_id_) return;
		std::lock_guard lock(join_mutex_);
		if (yield_thread_.joinable()) yield_thread_.join();
	}

	bool Requestor::is_multithreaded() {
		return false;
	}

	std::shared_ptr<Requestor> Requestor::make_shared() {
		auto ptr = std::make_shared<Requestor>(private_constructor{});
		ptr->yield_thread_ = std::thread([ptr]() mutable {
			// Publish the thread object/id before the worker can finish or call shutdown.
			ptr->started_.wait();
			Requestor* self = ptr.get();
			self->yield_executor(std::move(ptr));
		});
		ptr->worker_id_ = ptr->yield_thread_.get_id();
		ptr->started_.count_down();
		return ptr;
	}

	void Requestor::yield_executor(std::shared_ptr<Requestor> shared_this) {
		try {
			while (!shutting_down_.load(std::memory_order_acquire)) {
				if (shared_this.use_count() == 1) {
					shutting_down_.store(true, std::memory_order_release);
					break;
				}
				yield(poll_timeout_ms);
			}
		} catch (...) {
			// Preserve the fatal cause for active and subsequent requests.
			fail_all(std::current_exception());
			shutting_down_.store(true, std::memory_order_release);
		}
		cleanup();
	}
}
