#include <asyncnet/CurlMulti.hpp>
#include <asyncnet/Exceptions.hpp>
#include <curlpp/Options.hpp>
#include <curlpp/Exception.hpp>
#include <utility>

namespace asyncnet {
namespace {
constexpr int curl_continue_request = 0;
constexpr int curl_cancel_request = 1;

size_t header_callback(char* buffer, size_t size, size_t count, void* userdata) {
	auto& block = *static_cast<std::string*>(userdata);
	const std::string_view line(buffer, size * count);
	if (line.starts_with("HTTP/")) block.clear();
	else block.append(line);
	return line.size();
}
}

// Own the easy handle and response buffers independently of the awaiting frame.
struct CurlMulti::Transfer {
	explicit Transfer(curlpp::Easy easy, std::stop_token token, std::ostream* output, NetworkPromise<Response>& progress_sink)
	    : stop(std::move(token)), progress_sink(progress_sink), handle(std::move(easy)) {
		if (!output) {
			body.emplace();
			output = &*body;
		}
		handle.setOpt(curlpp::options::WriteStream(output));
		handle.setOpt(curlpp::options::NoProgress(false));
		curl_easy_setopt(handle.getHandle(), CURLOPT_HEADERFUNCTION, &header_callback);
		curl_easy_setopt(handle.getHandle(), CURLOPT_HEADERDATA, &headers);
		curl_easy_setopt(handle.getHandle(), CURLOPT_XFERINFOFUNCTION, &progress);
		curl_easy_setopt(handle.getHandle(), CURLOPT_XFERINFODATA, this);
	}

	static int progress(void* data, curl_off_t total, curl_off_t now, curl_off_t, curl_off_t) {
		auto& transfer = *static_cast<Transfer*>(data);
		transfer.total_bytes = total;
		transfer.read_bytes = now;
		transfer.progress_sink.set_total_bytes(total);
		transfer.progress_sink.set_read_bytes(now);
		return transfer.stop.stop_requested() ? curl_cancel_request : curl_continue_request;
	}

	// Handle is destroyed first, while callback storage is still alive.
	std::optional<std::ostringstream> body;
	std::string headers;
	std::stop_token stop;
	// Borrowed only while registered; abandon removes the handle under the same
	// lock as callbacks before this promise can be destroyed.
	NetworkPromise<Response>& progress_sink;
	curl_off_t total_bytes = 0;
	curl_off_t read_bytes = 0;
	std::coroutine_handle<> continuation;
	CURLcode result = CURLE_OK;
	std::exception_ptr failure;
	bool registered = false;
	curlpp::Easy handle;
};

class CurlMulti::TransferAwaiter {
public:
	TransferAwaiter(CurlMulti& owner, std::shared_ptr<Transfer> transfer)
	    : owner_(owner), transfer_(std::move(transfer)) {}
	~TransferAwaiter() { owner_.abandon(transfer_); }
	bool await_ready() const noexcept { return false; }
	bool await_suspend(std::coroutine_handle<> continuation) {
		auto lock = owner_.interrupt_lock();
		if (owner_.failure_) {
			transfer_->failure = owner_.failure_;
			return false;
		}
		if (owner_.closed_ || transfer_->stop.stop_requested()) {
			transfer_->result = CURLE_ABORTED_BY_CALLBACK;
			return false;
		}
		auto* handle = transfer_->handle.getHandle();
		owner_.transfers_.emplace(handle, transfer_);
		const auto code = curl_multi_add_handle(owner_.handle_, handle);
		if (code != CURLM_OK) {
			owner_.transfers_.erase(handle);
			throw RuntimeError("Cannot add curl multi handle");
		}
		transfer_->registered = true;
		transfer_->continuation = continuation;
		// Nothing may access the awaiter after publishing its continuation/unlocking.
		return true;
	}
	void await_resume() const {
		if (transfer_->failure) std::rethrow_exception(transfer_->failure);
	}
private:
	CurlMulti& owner_;
	std::shared_ptr<Transfer> transfer_;
};

CurlMulti::CurlMulti() : CurlMulti(curl_multi_init()) {
	curlpp::runtimeAssert("Cannot initialize curl multi handle", handle_ != nullptr);
}
CurlMulti::CurlMulti(CURLM* handle) noexcept : handle_(handle) {}
CurlMulti::~CurlMulti() noexcept {
	cleanup();
	curl_multi_cleanup(handle_);
}

void CurlMulti::finish_locked(CURL* handle, CURLcode code, Completions& ready) {
	auto it = transfers_.find(handle);
	if (it == transfers_.end()) return;
	auto transfer = it->second;
	ready.push_back(transfer);
	curl_multi_remove_handle(handle_, handle);
	transfer->registered = false;
	transfer->result = code;
	transfers_.erase(it);
}
void CurlMulti::collect_finished_locked(Completions& ready) {
	int remaining;
	while (auto* message = curl_multi_info_read(handle_, &remaining)) {
		if (message->msg == CURLMSG_DONE)
			finish_locked(message->easy_handle, message->data.result, ready);
	}
}
void CurlMulti::collect_cancelled_locked(Completions& ready) {
	for (auto it = transfers_.begin(); it != transfers_.end();) {
		auto current = it++;
		if (current->second->stop.stop_requested())
			finish_locked(current->first, CURLE_ABORTED_BY_CALLBACK, ready);
	}
}
void CurlMulti::resume_ready(Completions ready) {
	for (auto& transfer : ready) {
		std::coroutine_handle<> continuation;
		{
			std::lock_guard lock(mutex_);
			continuation = std::exchange(transfer->continuation, {});
		}
		// Reentrant requests, shutdown and a continuation's next suspension are safe:
		// no transport lock or owning temporary notification task crosses resume().
		if (continuation) continuation.resume();
	}
}
std::unique_lock<std::mutex> CurlMulti::interrupt_lock() {
	// Wake before waiting on the mutex held by poll. Keep a pending marker until
	// acquired so the poller cannot consume the wakeup and enter another long poll.
	pending_operations_.fetch_add(1, std::memory_order_relaxed);
	wakeup_polling();
	std::unique_lock lock(mutex_);
	pending_operations_.fetch_sub(1, std::memory_order_relaxed);
	return lock;
}
void CurlMulti::abandon(const std::shared_ptr<Transfer>& transfer) {
	auto lock = interrupt_lock();
	transfer->continuation = {};
	if (transfer->registered) {
		curl_multi_remove_handle(handle_, transfer->handle.getHandle());
		transfers_.erase(transfer->handle.getHandle());
		transfer->registered = false;
	}
}

int CurlMulti::yield(int timeout_ms) {
	Completions ready;
	std::exception_ptr failure;
	{
		std::lock_guard lock(mutex_);
		try {
			if (failure_) std::rethrow_exception(failure_);
			collect_cancelled_locked(ready);
			int running;
			if (curl_multi_perform(handle_, &running) != CURLM_OK)
				throw RuntimeError("Cannot perform curl multi requests");
			collect_finished_locked(ready);
			if (ready.empty() && !closed_ && pending_operations_.load(std::memory_order_relaxed) == 0) {
				int events;
				if (curl_multi_poll(handle_, nullptr, 0, timeout_ms, &events) != CURLM_OK)
					throw RuntimeError("Cannot poll curl multi requests");
			}
			collect_cancelled_locked(ready);
		} catch (...) {
			failure = std::current_exception();
			fail_locked(failure, ready);
		}
	}
	// Removed requests are no longer reachable by cleanup(). Deliver them even
	// when a later multi operation fails, and only after releasing the mutex.
	resume_ready(std::move(ready));
	if (failure) std::rethrow_exception(failure);
	std::lock_guard lock(mutex_);
	return static_cast<int>(transfers_.size());
}
int CurlMulti::deliver_finished() {
	Completions ready;
	{ std::lock_guard lock(mutex_); collect_finished_locked(ready); }
	const int count = static_cast<int>(ready.size());
	resume_ready(std::move(ready));
	return count;
}
int CurlMulti::abort_cancelled() {
	Completions ready;
	{ auto lock = interrupt_lock(); collect_cancelled_locked(ready); }
	const int count = static_cast<int>(ready.size());
	resume_ready(std::move(ready));
	return count;
}
void CurlMulti::abort_all() {
	Completions ready;
	{
		auto lock = interrupt_lock();
		while (!transfers_.empty())
			finish_locked(transfers_.begin()->first, CURLE_ABORTED_BY_CALLBACK, ready);
	}
	resume_ready(std::move(ready));
}
void CurlMulti::fail_locked(std::exception_ptr failure, Completions& ready) {
	closed_ = true;
	if (!failure_) failure_ = failure;
	// Preserve results already reported by curl, including those from a perform
	// that subsequently returned an error. Only unfinished transfers fail.
	collect_finished_locked(ready);
	collect_cancelled_locked(ready);
	while (!transfers_.empty()) {
		auto transfer = transfers_.begin()->second;
		finish_locked(transfers_.begin()->first, CURLE_OK, ready);
		transfer->failure = failure_;
	}
}
void CurlMulti::fail_all(std::exception_ptr failure) {
	Completions ready;
	{
		auto lock = interrupt_lock();
		fail_locked(failure, ready);
	}
	resume_ready(std::move(ready));
}
void CurlMulti::rethrow_failure() {
	auto lock = interrupt_lock();
	if (failure_) std::rethrow_exception(failure_);
}
void CurlMulti::cleanup() {
	{
		auto lock = interrupt_lock();
		closed_ = true;
	}
	abort_all();
}

NetworkTask<Response> CurlMulti::perform_handle(curlpp::Easy handle, std::ostream* output_stream) {
	auto& promise = co_await awaitables::get_self;
	auto transfer = std::make_shared<Transfer>(std::move(handle), promise.stop_source().get_token(), output_stream, promise);
	std::stop_callback wake_on_stop(transfer->stop, [this] { wakeup_polling(); });
	TransferAwaiter awaiter(*this, transfer);
	co_await awaiter;
	promise.set_read_bytes(transfer->read_bytes);
	promise.set_total_bytes(transfer->total_bytes);
	curlpp::libcurlRuntimeAssert("Multi network request failed", transfer->result);
	if (transfer->body)
		co_return Response(std::move(transfer->handle), std::move(*transfer->body), std::move(transfer->headers));
	co_return Response(std::move(transfer->handle), std::move(transfer->headers));
}
CURLM* CurlMulti::get_handle() const noexcept { return handle_; }
void CurlMulti::wakeup_polling() const { curl_multi_wakeup(handle_); }
}
