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
		// Only handle_ transfers. mutex_ and condition_awaiters_ are deliberately NOT
		// carried over: an in-flight perform_handle() is a coroutine bound to `other`'s
		// `this`, holding `other`'s mutex and an awaiter reference, and coro::mutex is
		// not movable — so moving a CurlMulti with pending requests is unsound no matter
		// where the map ends up. Precondition: `other` is idle (idle <=> empty map).
		assert(other.condition_awaiters_.empty() &&
			"moving a CurlMulti with in-flight requests is undefined");
	}

	CurlMulti::~CurlMulti() {
		// Only abort if requests are still pending. In the Requestor teardown path
		// cleanup() already ran while the object was fully alive, leaving this empty,
		// so skipping avoids a redundant nested sync_wait() here (on the detached
		// worker thread). Reading unlocked is safe: an object being destroyed must
		// not be used concurrently.
		if (!condition_awaiters_.empty()) {
			coro::sync_wait(cleanup());
		}
		curl_multi_cleanup(handle_);
	}

	CurlMulti& CurlMulti::operator=(CurlMulti&& other) noexcept {
		if (std::addressof(other) != this) {
			if (!condition_awaiters_.empty()) {
				coro::sync_wait(cleanup());
			}
			curl_multi_cleanup(handle_);
			handle_ = std::exchange(other.handle_, nullptr);
		}
		return *this;
	}

	coro::task<int> CurlMulti::yield(int timeout_ms) {
		int running_handles;
		CURLMcode error_code = curl_multi_perform(handle_, &running_handles);
		if (error_code != CURLM_OK) {
			throw RuntimeError("Error when trying to curl_multi_perform()");
		}

		//if (running_handles == 0) {
		//	co_return 0;
		//}

		int events_count;
		error_code = curl_multi_poll(handle_, nullptr, 0, timeout_ms, &events_count);
		if (error_code != CURLM_OK) {
			throw RuntimeError("Error when trying to curl_multi_poll()");
		}

		CURLMsg* msg;
		int msg_count;
		msg = curl_multi_info_read(handle_, &msg_count);

		while(msg) {
			if (msg && msg->msg == CURLMSG_DONE) {
				auto& handle_awaiter = condition_awaiters_.at(msg->easy_handle);
				curl_multi_remove_handle(handle_, msg->easy_handle);
				handle_awaiter.exit_code = msg->data.result;
				handle_awaiter.cv.notify_one().resume();
				// performer is responsible for cleaning up awaiters
			}
			msg = curl_multi_info_read(handle_, &msg_count);
		}

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