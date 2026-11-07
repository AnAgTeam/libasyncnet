#include "catch_amalgamated.hpp"
#include <asyncnet/CurlMulti.hpp>
#include <asyncnet/Requestor.hpp>
#include <asyncnet/Exceptions.hpp>
#include <coro/sync_wait.hpp>
#include <coro/when_all.hpp>
#include <curlpp/Options.hpp>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <future>
#include <latch>
#include <random>
#include <thread>

using namespace asyncnet;
namespace {
using namespace std::chrono_literals;
constexpr auto test_deadline = 5s;
constexpr int no_wait = 0;
constexpr int stress_requests = 64;

struct FileFixture {
	std::filesystem::path path = std::filesystem::temp_directory_path() /
	    ("asyncnet-lifecycle-" + std::to_string(std::random_device{}()));
	const std::string body = "local transport fixture\n";
	FileFixture() { std::ofstream(path, std::ios::binary) << body; }
	~FileFixture() { std::error_code error; std::filesystem::remove(path, error); }
	curlpp::Easy handle() const {
		curlpp::Easy easy;
		easy.setOpt(curlpp::options::Url("file://" + path.string()));
		return easy;
	}
};

void drain(CurlMulti& multi, NetworkTask<Response>& task) {
	const auto deadline = std::chrono::steady_clock::now() + test_deadline;
	while (!task.done() && std::chrono::steady_clock::now() < deadline) multi.yield(no_wait);
	REQUIRE(task.done());
}

NetworkTask<void> sequential(CurlMulti& multi, const FileFixture& file, int& completed) {
	for (int index = 0; index < stress_requests; ++index) {
		auto response = co_await multi.perform_handle(file.handle());
		if (response.get_text() == file.body) ++completed;
	}
}

struct Park {
	std::coroutine_handle<>& parked;
	bool await_ready() const noexcept { return false; }
	void await_suspend(std::coroutine_handle<> handle) const noexcept { parked = handle; }
	void await_resume() const noexcept {}
};
NetworkTask<void> park_after_response(CurlMulti& multi, const FileFixture& file,
                                    std::coroutine_handle<>& parked, std::thread::id& resumed) {
	co_await multi.perform_handle(file.handle());
	co_await Park{parked};
	resumed = std::this_thread::get_id();
}
NetworkTask<void> abort_from_continuation(CurlMulti& multi, const FileFixture& file, int& completed) {
	co_await multi.perform_handle(file.handle());
	multi.abort_all();
	++completed;
}
NetworkTask<void> shutdown_from_continuation(Requestor& requestor, const FileFixture& file,
                                           std::promise<void>& finished) {
	co_await requestor.perform_handle(file.handle());
	requestor.shutdown();
	finished.set_value();
}
NetworkTask<void> destroy_other_after_response(CurlMulti& multi, const FileFixture& file,
                                             std::optional<NetworkTask<Response>>& other) {
	co_await multi.perform_handle(file.handle());
	other.reset();
}
}

TEST_CASE("Multi owns a response independently of its task", "[lifecycle]") {
	FileFixture file;
	CurlMulti multi;
	std::optional<Response> response;
	{
		auto task = multi.perform_handle(file.handle());
		task.resume();
		drain(multi, task);
		response.emplace(std::move(task.promise()).result());
	}
	multi.cleanup();
	CHECK(response->get_text() == file.body);
}

TEST_CASE("Multi preserves an explicit body sink", "[lifecycle]") {
	FileFixture file;
	CurlMulti multi;
	std::ostringstream sink;
	auto task = multi.perform_handle(file.handle(), &sink);
	task.resume();
	drain(multi, task);
	CHECK(sink.str() == file.body);
	CHECK(task.promise().result().get_text().empty());
}

TEST_CASE("Destroying a registered suspended transfer removes it", "[lifecycle]") {
	FileFixture file;
	CurlMulti multi;
	for (int index = 0; index < stress_requests; ++index) {
		auto task = multi.perform_handle(file.handle());
		task.resume();
		REQUIRE_FALSE(task.done());
	}
	CHECK(multi.yield(no_wait) == 0);
	CHECK(multi.abort_cancelled() == 0);
	auto next = multi.perform_handle(file.handle());
	next.resume();
	drain(multi, next);
	CHECK(next.promise().result().get_text() == file.body);
}

TEST_CASE("Destroying a never started transfer has no transport side effects", "[lifecycle]") {
	FileFixture file;
	CurlMulti multi;
	{ auto task = multi.perform_handle(file.handle()); }
	CHECK(multi.yield(no_wait) == 0);
}

TEST_CASE("Cancellation before start never registers a transfer", "[lifecycle]") {
	FileFixture file;
	CurlMulti multi;
	auto task = multi.perform_handle(file.handle());
	task.request_stop();
	task.resume();
	REQUIRE(task.done());
	CHECK_THROWS_AS(task.promise().result(), NetworkRuntimeError);
	CHECK(multi.abort_cancelled() == 0);
}

TEST_CASE("Pending cancellation is delivered exactly once", "[lifecycle]") {
	FileFixture file;
	CurlMulti multi;
	auto task = multi.perform_handle(file.handle());
	task.resume();
	task.request_stop();
	CHECK(multi.abort_cancelled() == 1);
	REQUIRE(task.done());
	CHECK_THROWS_AS(task.promise().result(), NetworkRuntimeError);
	CHECK(multi.abort_cancelled() == 0);
	CHECK(multi.deliver_finished() == 0);
}

TEST_CASE("Abort all drains multiple waiters and allows later requests", "[lifecycle]") {
	FileFixture file;
	CurlMulti multi;
	std::vector<NetworkTask<Response>> tasks;
	for (int index = 0; index < stress_requests; ++index) {
		tasks.push_back(multi.perform_handle(file.handle()));
		tasks.back().resume();
	}
	multi.abort_all();
	for (auto& task : tasks) {
		REQUIRE(task.done());
		CHECK_THROWS_AS(task.promise().result(), NetworkRuntimeError);
	}
	multi.abort_all();
	auto next = multi.perform_handle(file.handle());
	next.resume();
	drain(multi, next);
	CHECK_NOTHROW(next.promise().result());
}

TEST_CASE("Cleanup is terminal and idempotent", "[lifecycle]") {
	FileFixture file;
	CurlMulti multi;
	auto pending = multi.perform_handle(file.handle());
	pending.resume();
	multi.cleanup();
	multi.cleanup();
	REQUIRE(pending.done());
	CHECK_THROWS_AS(pending.promise().result(), NetworkRuntimeError);
	auto rejected = multi.perform_handle(file.handle());
	rejected.resume();
	REQUIRE(rejected.done());
	CHECK_THROWS_AS(rejected.promise().result(), NetworkRuntimeError);
}

TEST_CASE("Continuations can register the next request without holding the multi lock", "[lifecycle]") {
	FileFixture file;
	CurlMulti multi;
	int completed = 0;
	auto task = sequential(multi, file, completed);
	task.resume();
	const auto deadline = std::chrono::steady_clock::now() + test_deadline;
	while (!task.done() && std::chrono::steady_clock::now() < deadline) multi.yield(no_wait);
	REQUIRE(task.done());
	CHECK_NOTHROW(task.promise().result());
	CHECK(completed == stress_requests);
}

TEST_CASE("Suspending a continuation cannot suspend or migrate the poll loop", "[lifecycle]") {
	FileFixture file;
	CurlMulti multi;
	std::coroutine_handle<> parked;
	std::thread::id resumed;
	const auto poll_thread = std::this_thread::get_id();
	auto task = park_after_response(multi, file, parked, resumed);
	task.resume();
	multi.yield(no_wait);
	REQUIRE(parked);
	REQUIRE_FALSE(task.done());
	auto next = multi.perform_handle(file.handle());
	next.resume();
	drain(multi, next);
	std::thread foreign([&] { parked.resume(); });
	foreign.join();
	REQUIRE(task.done());
	CHECK(resumed != poll_thread);
	CHECK(std::this_thread::get_id() == poll_thread);
	CHECK(multi.yield(no_wait) == 0);
}

TEST_CASE("A completion may reenter abort all", "[lifecycle]") {
	FileFixture file;
	CurlMulti multi;
	int completed = 0;
	auto task = abort_from_continuation(multi, file, completed);
	task.resume();
	multi.yield(no_wait);
	REQUIRE(task.done());
	CHECK(completed == 1);
}

TEST_CASE("A completion may destroy another waiter in the same ready batch", "[lifecycle]") {
	FileFixture file;
	CurlMulti multi;
	std::optional<NetworkTask<Response>> other;
	auto first = destroy_other_after_response(multi, file, other);
	first.resume();
	other.emplace(multi.perform_handle(file.handle()));
	other->resume();
	multi.yield(no_wait);
	REQUIRE(first.done());
	CHECK_FALSE(other.has_value());
	CHECK(multi.yield(no_wait) == 0);
}

TEST_CASE("Concurrent producers share one serialized multi", "[lifecycle]") {
	FileFixture file;
	auto requestor = Requestor::make_shared();
	constexpr int producers = 4;
	std::latch start(producers);
	std::atomic<int> completed = 0;
	std::vector<std::thread> threads;
	for (int producer = 0; producer < producers; ++producer) {
		threads.emplace_back([&] {
			start.arrive_and_wait();
			for (int index = 0; index < stress_requests; ++index) {
				auto response = coro::sync_wait(requestor->perform_handle(file.handle()));
				if (response.get_text() == file.body) completed.fetch_add(1);
			}
		});
	}
	for (auto& thread : threads) thread.join();
	requestor->shutdown();
	CHECK(completed == producers * stress_requests);
}

TEST_CASE("Shutdown called by a completion never joins its own thread", "[lifecycle]") {
	FileFixture file;
	auto requestor = Requestor::make_shared();
	std::promise<void> finished;
	auto future = finished.get_future();
	auto task = shutdown_from_continuation(*requestor, file, finished);
	task.resume();
	REQUIRE(future.wait_for(test_deadline) == std::future_status::ready);
	requestor->shutdown();
	REQUIRE(task.done());
	CHECK_NOTHROW(task.promise().result());
}

TEST_CASE("Concurrent shutdown callers wait for the same worker", "[lifecycle]") {
	auto requestor = Requestor::make_shared();
	std::vector<std::thread> threads;
	for (int index = 0; index < 4; ++index)
		threads.emplace_back([&] { requestor->shutdown(); });
	for (auto& thread : threads) thread.join();
	FileFixture file;
	CHECK_THROWS_AS(coro::sync_wait(requestor->perform_handle(file.handle())), RuntimeError);
}

TEST_CASE("Worker releases the last owner after automatic shutdown", "[lifecycle]") {
	auto requestor = Requestor::make_shared();
	std::weak_ptr<Requestor> weak = requestor;
	requestor.reset();
	const auto deadline = std::chrono::steady_clock::now() + test_deadline;
	while (!weak.expired() && std::chrono::steady_clock::now() < deadline)
		std::this_thread::sleep_for(1ms);
	CHECK(weak.expired());
}

#if defined(__unix__) || defined(__APPLE__)
#include <sys/socket.h>
#include <netinet/in.h>
#include <poll.h>
#include <unistd.h>

namespace {
// Accept a connection but never send HTTP headers. Cancellation is therefore
// exercised during real I/O, independently of file:// finishing in one perform.
class StalledServer {
public:
	StalledServer() {
		listener_ = ::socket(AF_INET, SOCK_STREAM, 0);
		if (listener_ < 0) throw std::runtime_error("socket failed");
		sockaddr_in address{};
		address.sin_family = AF_INET;
		address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
		if (::bind(listener_, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
		    ::listen(listener_, stress_requests) != 0) {
			::close(listener_);
			throw std::runtime_error("loopback bind/listen failed");
		}
		socklen_t length = sizeof(address);
		::getsockname(listener_, reinterpret_cast<sockaddr*>(&address), &length);
		port_ = ntohs(address.sin_port);
	}
	~StalledServer() {
		for (int client : clients_) ::close(client);
		::close(listener_);
	}
	void accept_one() {
		pollfd listener{listener_, POLLIN, 0};
		const auto timeout_ms = std::chrono::duration_cast<std::chrono::milliseconds>(test_deadline).count();
		if (::poll(&listener, 1, static_cast<int>(timeout_ms)) != 1)
			throw std::runtime_error("client did not connect");
		int client = ::accept(listener_, nullptr, nullptr);
		if (client < 0) throw std::runtime_error("accept failed");
		clients_.push_back(client);
	}
	curlpp::Easy handle() const {
		curlpp::Easy easy;
		easy.setOpt(curlpp::options::Url("http://127.0.0.1:" + std::to_string(port_) + "/"));
		easy.setOpt(curlpp::options::Proxy(""));
		// Bound failed-test teardown even if the cancellation wakeup regresses.
		easy.setOpt(curlpp::options::Timeout(3L));
		return easy;
	}
private:
	int listener_;
	unsigned short port_;
	std::vector<int> clients_;
};

CURLcode wait_result(NetworkTask<Response> task) {
	try {
		coro::sync_wait(std::move(task));
		return CURLE_OK;
	} catch (const NetworkRuntimeError& error) {
		return error.whatCode();
	}
}
}

TEST_CASE("Stop wakes the poller during stalled HTTP I/O", "[lifecycle][loopback]") {
	StalledServer server;
	auto requestor = Requestor::make_shared();
	auto task = requestor->perform_handle(server.handle());
	auto stop = task.get_stop_source();
	auto result = std::async(std::launch::async, wait_result, std::move(task));
	server.accept_one();
	const auto before = std::chrono::steady_clock::now();
	stop.request_stop();
	REQUIRE(result.wait_for(test_deadline) == std::future_status::ready);
	CHECK(result.get() == CancelledErrorCode);
	// Clearly below the three-second transfer timeout; tolerate loaded CI hosts.
	CHECK(std::chrono::steady_clock::now() - before < 2s);
	requestor->shutdown();
}

TEST_CASE("Shutdown cancels every active stalled HTTP request", "[lifecycle][loopback]") {
	StalledServer server;
	auto requestor = Requestor::make_shared();
	constexpr int concurrent_requests = 12;
	std::vector<std::future<CURLcode>> results;
	for (int index = 0; index < concurrent_requests; ++index) {
		results.push_back(std::async(std::launch::async, wait_result, requestor->perform_handle(server.handle())));
		server.accept_one();
	}
	requestor->shutdown();
	for (auto& result : results) {
		REQUIRE(result.wait_for(test_deadline) == std::future_status::ready);
		CHECK(result.get() == CancelledErrorCode);
	}
}

TEST_CASE("Cancelled stalled transfers leave the requestor usable", "[lifecycle][loopback]") {
	StalledServer server;
	FileFixture file;
	auto requestor = Requestor::make_shared();
	for (int index = 0; index < 16; ++index) {
		auto task = requestor->perform_handle(server.handle());
		auto stop = task.get_stop_source();
		auto result = std::async(std::launch::async, wait_result, std::move(task));
		server.accept_one();
		stop.request_stop();
		REQUIRE(result.wait_for(test_deadline) == std::future_status::ready);
		CHECK(result.get() == CancelledErrorCode);
		CHECK(coro::sync_wait(requestor->perform_handle(file.handle())).get_text() == file.body);
	}
	requestor->shutdown();
}
#endif

TEST_CASE("Many simultaneous completions race with stop requests", "[lifecycle][stress]") {
	FileFixture file;
	auto requestor = Requestor::make_shared();
	constexpr int rounds = 24;
	for (int round = 0; round < rounds; ++round) {
		std::vector<NetworkTask<Response>> tasks;
		std::vector<std::stop_source> stops;
		for (int index = 0; index < stress_requests; ++index) {
			tasks.push_back(requestor->perform_handle(file.handle()));
			stops.push_back(tasks.back().get_stop_source());
		}
		std::thread stopper([&] {
			// Leave half running so completion and cancellation both stay busy.
			for (std::size_t index = 0; index < stops.size(); index += 2) {
				std::this_thread::yield();
				stops[index].request_stop();
			}
		});
		auto results = coro::sync_wait(coro::when_all(std::move(tasks)));
		stopper.join();
		for (auto& result : results) {
			try {
				auto& response = result.return_value();
				CHECK(response.get_text() == file.body);
			} catch (const NetworkRuntimeError& error) {
				CHECK(error.whatCode() == CancelledErrorCode);
			}
		}
	}
	requestor->shutdown();
}

#if defined(__unix__) || defined(__APPLE__)
TEST_CASE("Destroying a stalled waiter detaches callbacks before releasing its frame", "[lifecycle][loopback]") {
	StalledServer server;
	FileFixture file;
	auto requestor = Requestor::make_shared();
	{
		auto task = requestor->perform_handle(server.handle());
		// The server cannot complete this transfer; destruction does not race resume.
		task.handle().resume();
		server.accept_one();
	}
	CHECK(coro::sync_wait(requestor->perform_handle(file.handle())).get_text() == file.body);
	requestor->shutdown();
}
#endif

namespace {
int fail_timer_when_armed(CURLM*, long timeout_ms, void* data) {
	// libcurl's timer callback contract: -1 reports a callback failure.
	// Ignore immediate/removal timers: fail when perform schedules network I/O.
	return *static_cast<bool*>(data) && timeout_ms > 0 ? -1 : 0;
}
}

#if defined(__unix__) || defined(__APPLE__)
TEST_CASE("Multi error still delivers cancellations already removed from its map", "[lifecycle][failure]") {
	FileFixture file;
	bool fail_timer = false;
	CurlMulti multi;
	REQUIRE(curl_multi_setopt(multi.get_handle(), CURLMOPT_TIMERFUNCTION, &fail_timer_when_armed) == CURLM_OK);
	REQUIRE(curl_multi_setopt(multi.get_handle(), CURLMOPT_TIMERDATA, &fail_timer) == CURLM_OK);
	auto cancelled = multi.perform_handle(file.handle());
	StalledServer server;
	auto active = multi.perform_handle(server.handle());
	cancelled.resume();
	active.resume();
	cancelled.request_stop();
	fail_timer = true;

	// This injects a real CURLM_ABORTED_BY_CALLBACK, without replacing libcurl or
	// adding a production test hook. The cancellation precedes the failed perform.
	CHECK_THROWS_AS(multi.yield(no_wait), RuntimeError);
	CHECK(cancelled.done());
	if (cancelled.done()) CHECK_THROWS_AS(cancelled.promise().result(), NetworkRuntimeError);
	CHECK(active.done());
	if (active.done())
		CHECK_THROWS_WITH(active.promise().result(), "Cannot perform curl multi requests");
	fail_timer = false;
	multi.cleanup();
	auto later = multi.perform_handle(file.handle());
	later.resume();
	REQUIRE(later.done());
	CHECK_THROWS_WITH(later.promise().result(), "Cannot perform curl multi requests");
}


#endif

TEST_CASE("Fatal shutdown preserves queued results and the original failure", "[lifecycle][failure]") {
	FileFixture file;
	CurlMulti multi;
	auto completed = multi.perform_handle(file.handle());
	completed.resume();
	int running = 0;
	REQUIRE(curl_multi_perform(multi.get_handle(), &running) == CURLM_OK);
	// Leave CURLMSG_DONE queued, then register requests that have not run yet.
	auto active = multi.perform_handle(file.handle());
	auto cancelled = multi.perform_handle(file.handle());
	active.resume();
	cancelled.resume();
	cancelled.request_stop();

	multi.fail_all(std::make_exception_ptr(RuntimeError("original poll failure")));
	REQUIRE(completed.done());
	CHECK(completed.promise().result().get_text() == file.body);
	REQUIRE(cancelled.done());
	CHECK_THROWS_AS(cancelled.promise().result(), NetworkRuntimeError);
	REQUIRE(active.done());
	CHECK_THROWS_WITH(active.promise().result(), "original poll failure");

	multi.fail_all(std::make_exception_ptr(RuntimeError("secondary failure")));
	multi.cleanup();
	CHECK_THROWS_WITH(multi.rethrow_failure(), "original poll failure");
	auto later = multi.perform_handle(file.handle());
	later.resume();
	REQUIRE(later.done());
	CHECK_THROWS_WITH(later.promise().result(), "original poll failure");
}
