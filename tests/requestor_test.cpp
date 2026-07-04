#include "CoroTest.hpp"
#include <asyncnet/Requestor.hpp>
#include <asyncnet/Exceptions.hpp>

#include <coro/thread_pool.hpp>
#include <coro/sync_wait.hpp>
#include <coro/when_all.hpp>
#include <curlpp/Options.hpp>
#include <print>

#include <iostream>

#pragma execution_character_set("utf-8")

using namespace asyncnet;

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