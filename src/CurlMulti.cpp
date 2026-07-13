#include <asyncnet/CurlMulti.hpp>
#include <asyncnet/Exceptions.hpp>

#include <coro/sync_wait.hpp>
#include <curlpp/Easy.hpp>
#include <curlpp/Exception.hpp>
#include <curlpp/Options.hpp>
#include <numeric>
#include <ranges>
#include <string>
#include <string_view>

using std::views::iota;

constexpr int curl_cancel_request = 1;
constexpr int curl_continue_request = 0;

namespace asyncnet {

	CurlMulti::CurlMulti() : handle_(curl_multi_init()) {
		curlpp::runtimeAssert("Error when trying to curl_multi_init() a handle", handle_ != nullptr);
	}

	CurlMulti::CurlMulti(CURLM* handle) noexcept : handle_(handle) {}

	CurlMulti::CurlMulti(CurlMulti&& other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {
		
	}

	CurlMulti::~CurlMulti() {
		coro::sync_wait(cleanup());
		curl_multi_cleanup(handle_);
	}

	CurlMulti& CurlMulti::operator=(CurlMulti&& other) noexcept {
		if (std::addressof(other) != this) {
			coro::sync_wait(cleanup());
			curl_multi_cleanup(handle_);
			handle_ = std::exchange(other.handle_, nullptr);
		}
		return *this;
	}

	// Hand every transfer curl reports as finished to whoever is awaiting it, and
	// take the handle out of the multi. Returns how many were completed.
	int CurlMulti::deliver_finished() {
		int delivered = 0;
		int msg_count;
		for (CURLMsg* msg = curl_multi_info_read(handle_, &msg_count); msg;
		     msg = curl_multi_info_read(handle_, &msg_count)) {
			if (msg->msg != CURLMSG_DONE) {
				continue;
			}
			auto& handle_awaiter = condition_awaiters_.at(msg->easy_handle);
			curl_multi_remove_handle(handle_, msg->easy_handle);
			handle_awaiter.exit_code = msg->data.result;
			handle_awaiter.cv.notify_one().resume();
			// performer is responsible for cleaning up awaiters
			++delivered;
		}
		return delivered;
	}

	int CurlMulti::abort_cancelled() {
		// Snapshot first: resuming an awaiter below re-enters perform_handle, which
		// erases its own entry from condition_awaiters_ and would invalidate an
		// iterator held across the loop.
		std::vector<CURL*> cancelled;
		for (auto& [easy_handle, awaiter_context] : condition_awaiters_) {
			if (!awaiter_context.exit_code.has_value() && awaiter_context.promise
			    && awaiter_context.promise->stop_requested()) {
				cancelled.push_back(easy_handle);
			}
		}

		for (CURL* easy_handle : cancelled) {
			auto& awaiter_context = condition_awaiters_.at(easy_handle);
			curl_multi_remove_handle(handle_, easy_handle);
			awaiter_context.exit_code = CancelledErrorCode;
			awaiter_context.cv.notify_one().resume();
		}
		return static_cast<int>(cancelled.size());
	}

	coro::task<int> CurlMulti::yield(int timeout_ms) {
		int running_handles;
		CURLMcode error_code = curl_multi_perform(handle_, &running_handles);
		if (error_code != CURLM_OK) {
			throw RuntimeError("Error when trying to curl_multi_perform()");
		}

		abort_cancelled();

		int events_count;
		error_code = curl_multi_poll(handle_, nullptr, 0, timeout_ms, &events_count);
		if (error_code != CURLM_OK) {
			throw RuntimeError("Error when trying to curl_multi_poll()");
		}

		// Again after the poll: a stop request wakes it up (see perform_handle), and
		// this is where that cancellation gets acted on.
		abort_cancelled();
		deliver_finished();

		co_return running_handles;
	}

	coro::task<void> CurlMulti::cleanup() {
		return abort_all();
	}

	coro::task<void> CurlMulti::abort_all() {
		coro::scoped_lock lock = co_await mutex_.scoped_lock();
		for (auto& [handle, awaiter_context] : condition_awaiters_) {
			curl_multi_remove_handle(handle_, handle);
			awaiter_context.exit_code = CURLE_ABORTED_BY_CALLBACK;
			co_await awaiter_context.cv.notify_one();
		}
		assert(condition_awaiters_.empty());
	}

	// Report transfer progress and honour stop requests. Uses the modern
	// xferinfo callback (curl_off_t, exact 64-bit byte counts) rather than the
	// deprecated double-based progress function. curlpp only wraps the latter,
	// so it is set directly on the easy handle; clientp is the request's promise.
	static int xferinfo_callback(void* clientp, curl_off_t dl_total, curl_off_t dl_now,
		curl_off_t /*ul_total*/, curl_off_t /*ul_now*/) {
		auto& promise = *static_cast<NetworkPromise<Response>*>(clientp);
		promise.set_total_bytes(static_cast<long long>(dl_total));
		promise.set_read_bytes(static_cast<long long>(dl_now));
		return promise.stop_requested() ? curl_cancel_request : curl_continue_request;
	}

	// Accumulate the final response's header block. curl invokes this once per
	// header line (with a trailing CRLF) for every response in a redirect chain;
	// clearing on each new status line ("HTTP/...") keeps only the last response's
	// headers, and the status line itself is dropped so only "Name: Value" lines remain.
	static size_t header_callback(char* buffer, size_t size, size_t nitems, void* userdata) {
		const size_t total = size * nitems;
		auto& block = *static_cast<std::string*>(userdata);
		std::string_view line(buffer, total);
		if (line.starts_with("HTTP/")) {
			block.clear();
		} else {
			block.append(line);
		}
		return total;
	}

	NetworkTask<Response> CurlMulti::perform_handle(curlpp::Easy handle, std::ostream* output_stream) {
		NetworkPromise<Response>& promise = co_await awaitables::get_self;
		curl_easy_setopt(handle.getHandle(), CURLOPT_XFERINFOFUNCTION, &xferinfo_callback);
		curl_easy_setopt(handle.getHandle(), CURLOPT_XFERINFODATA, &promise);
		handle.setOpt(curlpp::options::NoProgress(false));

		// A stop request only takes effect inside the xferinfo callback, which curl
		// runs from curl_multi_perform() — and yield() is asleep in curl_multi_poll()
		// most of the time. Without this, a cancel sits unnoticed until the poll times
		// out. curl_multi_wakeup() is the one multi function that is safe to call from
		// another thread, which is exactly what request_stop() is. Lives on the
		// coroutine frame, so it is unregistered when the transfer is done.
		std::stop_callback wake_on_stop(promise.stop_source().get_token(),
			[this] { wakeup_polling(); });

		// Capture the response header block into a buffer owned by this coroutine
		// frame (stays alive across the suspend, like the body sink below).
		std::string header_block;
		curl_easy_setopt(handle.getHandle(), CURLOPT_HEADERFUNCTION, &header_callback);
		curl_easy_setopt(handle.getHandle(), CURLOPT_HEADERDATA, &header_block);

		// Route the body either into the caller's sink (streamed, not buffered) or,
		// when none is given, into an internal buffer owned by this coroutine frame.
		std::optional<std::ostringstream> owned_stream;
		std::ostream* sink = output_stream;
		if (!sink) {
			owned_stream.emplace();
			sink = &owned_stream.value();
		}
		handle.setOpt(curlpp::options::WriteStream(sink));

		coro::scoped_lock lock = co_await mutex_.scoped_lock();

		HandleAwaiterContext& context = condition_awaiters_[handle.getHandle()];
		context.promise = &promise;
		curl_multi_add_handle(handle_, handle.getHandle());
		curl_multi_wakeup(handle_);

		co_await context.cv.wait(lock, [&context] { return context.exit_code.has_value(); });
		// the lock is locked

		CURLcode exit_code = *context.exit_code;
		condition_awaiters_.erase(handle.getHandle());
		lock.unlock();

		curlpp::libcurlRuntimeAssert("Multi network request failed", exit_code);

		if (owned_stream) {
			co_return Response(std::move(handle), std::move(owned_stream.value()), std::move(header_block));
		}
		co_return Response(std::move(handle), std::move(header_block));
	}

	CURLM* CurlMulti::get_handle() const noexcept {
		return handle_;
	}

	void CurlMulti::wakeup_polling() const {
		curl_multi_wakeup(handle_);
	}
}