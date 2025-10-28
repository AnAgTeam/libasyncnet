#include "CoroTest.hpp"

#include <asyncnet/Request.hpp>
#include <asyncnet/CurlMulti.hpp>
#include <asyncnet/AsyncRequestor.hpp>
#include <coro/when_all.hpp>
#include <coro/thread_pool.hpp>

#include <iostream>

#pragma execution_character_set("utf-8")

using namespace asyncnet;

CORO_TEST_CASE("CurlMulti 2 GET requests") {
	GetRequest request1("https://www.google.com/");
	GetRequest request2("https://www.opennet.ru");

	CurlMulti multi;
	auto task1 = multi.perform_handle(request1.make_request_handle());
	auto task2 = multi.perform_handle(request2.make_request_handle());

	task1.resume();
	task2.resume();

	int remaining = 0;
	do {
		remaining = co_await multi.yield(1000);
	} while (remaining > 0);

	Response resp1 = co_await std::move(task1);
	Response resp2 = co_await std::move(task2);

	REQUIRE(resp1.get_status_code() == 200);
	REQUIRE(resp2.get_status_code() == 200);

	co_await multi.cleanup();
}

CORO_TEST_CASE("AsyncRequestor 2 GET requests") {
	GetRequest request1("https://www.google.com/");
	GetRequest request2("https://www.opennet.ru");

	auto requestor = AsyncRequestor::make_shared();

	auto [resp1, resp2] = co_await coro::when_all(requestor->perform_request(request1), requestor->perform_request(request2));

	REQUIRE(resp1.return_value().get_status_code() == 200);
	REQUIRE(resp2.return_value().get_status_code() == 200);
}