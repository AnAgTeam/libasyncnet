#include "catch_amalgamated.hpp"

#include <asyncnet/AsyncSession.hpp>
#include <asyncnet/NetworkTask.hpp>

#include <coro/sync_wait.hpp>
#include <print>

#pragma execution_character_set("utf-8")

using namespace asyncnet;

TEST_CASE("AsyncNetworkSession cancellation") {
	AsyncSession session(1);

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

TEST_CASE("AsyncNetworkSession normal get request") {
	AsyncSession session(1);

	auto worker = [](AsyncSession& session) -> coro::task<void> {
		auto request = session.make_request<GetRequest>("https://google.com");

		// REQUIRE_NOTHROW
		auto resp = co_await session.perform_request(request);
	};

	coro::sync_wait(worker(session));
}