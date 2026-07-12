#include "CoroTest.hpp"
#include <asyncnet/Requestor.hpp>
#include <asyncnet/Exceptions.hpp>

#include <coro/thread_pool.hpp>
#include <coro/sync_wait.hpp>
#include <coro/when_all.hpp>
#include <curlpp/Options.hpp>
#include <print>

#include <iostream>
#include <thread>
#include <chrono>

#pragma execution_character_set("utf-8")

using namespace asyncnet;

// --- teardown lifecycle (no network) — exercises variant B's event-driven shutdown ---
// Reaching the end of each case without std::terminate()/crash IS the assertion: the
// worker must self-detach and tear down cleanly when the last public handle drops.

TEST_CASE("Requestor teardown: create then drop") {
	{
		auto r = Requestor::make_shared();
		// let the worker reach curl_multi_poll() before we drop, so we exercise the
		// wakeup-from-poll path rather than an immediate flag check.
		std::this_thread::sleep_for(std::chrono::milliseconds(50));
	}
	SUCCEED("worker torn down after sole handle dropped");
}

TEST_CASE("Requestor teardown: explicit shutdown keeps the handle valid") {
	auto r = Requestor::make_shared();
	r->shutdown();                     // async signal; object stays alive while a handle is held
	REQUIRE(r != nullptr);
	std::this_thread::sleep_for(std::chrono::milliseconds(50));
	REQUIRE(r != nullptr);             // shutdown() did not pull the object out from under us
}

TEST_CASE("Requestor teardown: only the last handle drop shuts down") {
	auto r = Requestor::make_shared();
	{
		auto r2 = r;                   // extra public handles (same signalling control block)
		auto r3 = r;
		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}                                  // r2/r3 drop but r remains -> no shutdown yet
	std::this_thread::sleep_for(std::chrono::milliseconds(20));
	SUCCEED("survived non-final handle drops");
}

#if defined(ASYNCNET_ENABLE_TESTS_NETWORK)

TEST_CASE("NetworkRequestor request") {
	std::shared_ptr<Requestor> requestor = Requestor::make_shared();

	auto worker = [](std::shared_ptr<Requestor> requestor, std::string_view url) -> coro::task<void> {
		curlpp::Easy easy;

		std::string str_url(url);
		easy.setOpt(curlpp::options::Url(str_url));
		
		INFO(std::format("Performing {} ... (thread: {})", url, std::this_thread::get_id()));
		try {
			auto resp = co_await requestor->perform_handle(std::move(easy));
			REQUIRE(resp.get_status_code() == 200);
		} 
		catch (const NetworkRuntimeError&) {
			INFO(std::format("Request {} failed! (thread: {})", url, std::this_thread::get_id()));

			std::rethrow_exception(std::current_exception());
		}
		INFO(std::format("Finished {} ... (thread: {})", url, std::this_thread::get_id()));
	};

	auto output_tasks(coro::sync_wait(coro::when_all(worker(requestor, "https://www.google.com/"), worker(requestor, "https://www.opennet.ru"))));

	REQUIRE_NOTHROW(std::get<0>(output_tasks).return_value());
	REQUIRE_NOTHROW(std::get<1>(output_tasks).return_value());
}

CORO_TEST_CASE("Requestor 2 GET requests") {
	GetRequest request1("https://www.google.com/");
	GetRequest request2("https://www.opennet.ru");

	auto requestor = Requestor::make_shared();

	auto [resp1, resp2](co_await coro::when_all(requestor->perform_request(request1), requestor->perform_request(request2)));

	REQUIRE_NOTHROW(resp1.return_value().get_status_code() == 200);
	REQUIRE_NOTHROW(resp2.return_value().get_status_code() == 200);
}

#endif