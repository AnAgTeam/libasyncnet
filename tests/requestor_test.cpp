#include "catch_amalgamated.hpp"
#include <asyncnet/Requestor.hpp>
#include <coro/sync_wait.hpp>
#include <curlpp/Options.hpp>
#include <print>

#include <iostream>

#pragma execution_character_set("utf-8")

using namespace asyncnet;

//#define ASYNCNET_ENABLE_TESTS_NETWORK

TEST_CASE("Requestor copy, move") {
	Requestor requestor(2);
	Requestor requestor2 = requestor;
	Requestor requestor3 = std::move(requestor);
}

#if defined(ASYNCNET_ENABLE_TESTS_NETWORK)

TEST_CASE("NetworkRequestor request") {
	Requestor requestor(2);

	auto worker = [](Requestor& requestor, std::string_view url) -> coro::task<void> {
		curlpp::Easy easy;

		std::ostringstream stream;

		std::string str_url(url);
		easy.setOpt(curlpp::options::Url(str_url));
		easy.setOpt(curlpp::options::WriteStream(&stream));
		
		INFO(std::format("Performing {} ... (thread: {})", url, std::this_thread::get_id()));
		try {
			co_await requestor.perform_handle(easy);
		} 
		catch (const NetworkRequestError& e) {
			INFO(std::format("Request {} failed! (thread: {})", url, std::this_thread::get_id()));

			std::rethrow_exception(std::current_exception());
		}
		INFO(std::format("Finished {} ... (thread: {})", url, std::this_thread::get_id()));
	};

	auto start_time = std::chrono::steady_clock::now();
	auto output_tasks(coro::sync_wait(coro::when_all(worker(requestor, "https://google.com"), worker(requestor, "https://www.opennet.ru"))));
	auto end_time = std::chrono::steady_clock::now();

	REQUIRE_NOTHROW(std::get<0>(output_tasks).return_value());
	REQUIRE_NOTHROW(std::get<1>(output_tasks).return_value());
}

#endif

TEST_CASE("NetworkRequestor custom pool") {
	auto pool = coro::thread_pool::make_shared(coro::thread_pool::options{
		.thread_count = 1
	});

	Requestor requestor(2, pool);

	auto worker = [](Requestor& requestor, std::shared_ptr<coro::thread_pool> pool) -> coro::task<void> {
		co_await pool->schedule();
		auto pool_thread_id = std::this_thread::get_id();

		curlpp::Easy easy;

		std::ostringstream stream;

		easy.setOpt(curlpp::options::Url(""));
		easy.setOpt(curlpp::options::WriteStream(&stream));

		REQUIRE_THROWS_AS(co_await requestor.perform_handle(easy), NetworkRuntimeError);

		auto after_thread_id = std::this_thread::get_id();
		REQUIRE(pool_thread_id == after_thread_id);
	};

	coro::sync_wait(worker(requestor, pool));
}