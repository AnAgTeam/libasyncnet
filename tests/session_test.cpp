#include "catch_amalgamated.hpp"

#include <asyncnet/AsyncSession.hpp>
#include <asyncnet/Exceptions.hpp>

#include <coro/sync_wait.hpp>
#include <chrono>
#include <print>
#include <thread>

#pragma execution_character_set("utf-8")

using namespace asyncnet;

TEST_CASE("AsyncSession cancellation") {
	AsyncSession session;

	auto worker = [](AsyncSession& session) -> coro::task<void> {
		auto request = session.make_request<GetRequest>("https://google.com");

		try {
			auto task = session.perform_request(request);
			// should immediately stop
			task.request_stop();

			co_await task;

			// never reach here
			REQUIRE(false);
		}
		catch (const NetworkRuntimeError& e) {
			if (e.whatCode() != CancelledErrorCode) {
				std::rethrow_exception(std::current_exception());
			}
		}
	};

	coro::sync_wait(worker(session));
}

TEST_CASE("AsyncSession cancellation from other coroutine") {
	AsyncSession session;

	auto worker = [](AsyncSession& session) -> coro::task<void> {
		auto net_worker = [](AsyncSession& session) -> CancellingTask<void> {
			try {
				auto request = session.make_request<GetRequest>("https://google.com");
				auto task = session.perform_request(request).update_stop_source(co_await awaitables::get_stop_source);

				co_await task;

				// never reach here
				REQUIRE(false);
			}
			catch (const NetworkRuntimeError& e) {
				if (e.whatCode() != CancelledErrorCode) {
					std::rethrow_exception(std::current_exception());
				}
			}
		};
		
		auto task = net_worker(session);
		task.request_stop();
		co_await task;

	};

	coro::sync_wait(worker(session));
}

#ifdef ASYNCNET_ENABLE_TESTS_NETWORK

// The two cancellation tests above stop the request before it ever starts, so they
// only prove that Cancelled is reported. They stay green even if a cancel does not
// actually abort anything — an implementation that lets the transfer run to
// completion and only then reports Cancelled passes them just as well.
//
// The property that matters is that the transfer really is torn down, and the only
// thing that distinguishes that from ignoring the cancel is TIME. So: ask for a
// response the server will sit on for server_delay, cancel it once it is on the
// wire, and require the answer back long before the server would have replied.
namespace {
using namespace std::chrono_literals;

constexpr auto server_delay          = 5s;    // httpbin holds the response this long
constexpr auto cancel_after          = 300ms; // by now the request is on the wire
constexpr auto cancel_during_connect = 200ms; // still inside a connect that never lands
// Well above what a working cancel needs (~10ms after the stop) and well below what
// a broken one takes: the httpbin case would run the server's full 5s, and the
// mid-connect case was measured hanging ~2s before this was fixed.
constexpr auto must_stop_within      = 1s;
} // namespace

TEST_CASE("AsyncSession cancellation aborts a request that is already in flight") {
	using namespace std::chrono;

	AsyncSession session;

	auto worker = [](AsyncSession& session) -> coro::task<milliseconds> {
		auto net_worker = [](AsyncSession& session) -> CancellingTask<void> {
			auto request = session.make_request<GetRequest>("https://httpbin.org/delay/5");
			auto task = session.perform_request(request)
			                .update_stop_source(co_await awaitables::get_stop_source);
			try {
				co_await task;
			}
			catch (const NetworkRuntimeError& e) {
				if (e.whatCode() != CancelledErrorCode) {
					std::rethrow_exception(std::current_exception());
				}
				co_return;
			}
			// Completing means the cancel did nothing at all.
			REQUIRE(false);
		};

		auto task = net_worker(session);

		// Cancel from another thread, the way a UI does: request_stop() and the
		// curl_multi_wakeup() behind it are both safe to call from off-thread.
		std::thread canceller([&task] {
			std::this_thread::sleep_for(cancel_after);
			task.request_stop();
		});

		const auto started = steady_clock::now();
		co_await task;
		const auto elapsed = duration_cast<milliseconds>(steady_clock::now() - started);

		canceller.join();
		co_return elapsed;
	};

	const milliseconds elapsed = coro::sync_wait(worker(session));

	INFO("cancelled after " << cancel_after.count() << "ms, returned in " << elapsed.count()
	                        << "ms; the server would not have answered for "
	                        << duration_cast<milliseconds>(server_delay).count() << "ms");
	REQUIRE(elapsed < must_stop_within);
}

// The case above cancels a transfer whose connection is already established, and
// curl keeps calling the xferinfo callback while it waits for the body — so a stop
// gets noticed there on its own. While CONNECTING, curl calls no callback at all,
// so nothing observes the stop and the cancel is not seen until the connect either
// succeeds or times out. That is the hole abort_cancelled() fills, and only a test
// that cancels mid-connect can tell the difference.
//
// 10.255.255.1 is a private address nothing routes to: the TCP connect just hangs,
// which is precisely the window we need to cancel inside of.
TEST_CASE("AsyncSession cancellation aborts a request that is still connecting") {
	using namespace std::chrono;

	AsyncSession session;

	auto worker = [](AsyncSession& session) -> coro::task<milliseconds> {
		auto net_worker = [](AsyncSession& session) -> CancellingTask<void> {
			auto request = session.make_request<GetRequest>("http://10.255.255.1/");
			auto task = session.perform_request(request)
			                .update_stop_source(co_await awaitables::get_stop_source);
			try {
				co_await task;
			}
			catch (const NetworkRuntimeError& e) {
				// Anything other than Cancelled means the connect resolved on its own
				// (this network answers for that address) and the test proved nothing.
				REQUIRE(e.whatCode() == CancelledErrorCode);
				co_return;
			}
			REQUIRE(false);
		};

		auto task = net_worker(session);

		std::thread canceller([&task] {
			std::this_thread::sleep_for(cancel_during_connect);
			task.request_stop();
		});

		const auto started = steady_clock::now();
		co_await task;
		const auto elapsed = duration_cast<milliseconds>(steady_clock::now() - started);

		canceller.join();
		co_return elapsed;
	};

	const milliseconds elapsed = coro::sync_wait(worker(session));

	INFO("cancelled " << cancel_during_connect.count()
	                  << "ms into a connect that never completes; returned in "
	                  << elapsed.count() << "ms");
	REQUIRE(elapsed < must_stop_within);
}

TEST_CASE("AsyncSession GET request") {
	AsyncSession session;

	auto worker = [](AsyncSession& session) -> coro::task<void> {
		boost::json::object expected_args = {
			{ "test", "test1" }
		};

		auto request = session.make_request<GetRequest>("https://httpbin.org/get");
		request.set_url_parameters({
			{ "test", "test1" }
		});
		request.add_headers({
			"Test-Header: 123"
		});

		auto resp = co_await session.perform_request(request);
		CHECK(resp.get_status_code() == 200);

		boost::json::object resp_object = parse_json_object(resp.get_text());
		INFO(resp_object);

		boost::json::object resp_headers = resp_object["headers"].as_object();
		REQUIRE(resp_headers.find("Test-Header") != resp_headers.end());
		REQUIRE(resp_headers.find("Test-Header")->value().as_string() == "123");

		REQUIRE(resp_object["args"].as_object() == expected_args);
	};

	coro::sync_wait(worker(session));
}

TEST_CASE("AsyncSession POST request") {
	AsyncSession session;

	auto worker = [](AsyncSession& session) -> coro::task<void> {
		std::string test_data = "testdata=true&str=mmm";
		boost::json::object expected_json_form = {
			{ "str", "mmm" },
			{ "testdata", "true" }
		};

		auto request = session.make_request<PostRequest>("https://httpbin.org/post", test_data);

		auto resp = co_await session.perform_request(request);
		CHECK(resp.get_status_code() == 200);

		boost::json::object resp_object = parse_json_object(resp.get_text());
		INFO(resp_object);
		REQUIRE(resp_object["form"].as_object() == expected_json_form);
	};

	coro::sync_wait(worker(session));
}

TEST_CASE("AsyncSession GET request into output stream sink") {
	AsyncSession session;

	auto worker = [](AsyncSession& session) -> coro::task<void> {
		std::ostringstream sink;

		auto request = session.make_request<GetRequest>("https://httpbin.org/get");
		request.set_output_stream(&sink);

		auto resp = co_await session.perform_request(request);
		CHECK(resp.get_status_code() == 200);

		// body was streamed into the sink, not buffered in the Response
		REQUIRE(resp.get_text().empty());

		boost::json::object resp_object = parse_json_object(sink.str());
		INFO(resp_object);
		REQUIRE(resp_object.contains("url"));
	};

	coro::sync_wait(worker(session));
}

TEST_CASE("AsyncSession HEAD request") {
	AsyncSession session;

	auto worker = [](AsyncSession& session) -> coro::task<void> {
		auto request = session.make_request<HeadRequest>("https://httpbin.org/get");

		auto resp = co_await session.perform_request(request);
		CHECK(resp.get_status_code() == 200);

		REQUIRE(resp.get_status_code() == 200);
		REQUIRE(resp.get_text().empty());
	};

	coro::sync_wait(worker(session));
}

TEST_CASE("AsyncSession multipart POST request") {
	AsyncSession session;

	auto worker = [](AsyncSession& session) -> coro::task<void> {
		MultipartPart test_data_content = new MultipartContentPart("test", "test1");
		MultipartForms test_data_forms = { test_data_content };
		boost::json::object expected_json_form = {
			{ "test", "test1" }
		};

		auto request = session.make_request<PostMultipartRequest>("https://httpbin.org/post", test_data_forms);

		auto resp = co_await session.perform_request(request);
		CHECK(resp.get_status_code() == 200);

		boost::json::object resp_object = parse_json_object(resp.get_text());
		INFO(resp_object);

		REQUIRE(resp_object["form"].as_object() == expected_json_form);

		boost::json::object resp_headers = resp_object["headers"].as_object();
		REQUIRE(resp_headers.find("Content-Type") != resp_headers.end());
		REQUIRE(resp_headers.find("Content-Type")->value().as_string().starts_with("multipart/form-data;"));
	};

	coro::sync_wait(worker(session));
}

#endif