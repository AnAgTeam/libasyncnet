#include "catch_amalgamated.hpp"

#include <asyncnet/AsyncSession.hpp>

#include <coro/sync_wait.hpp>

#pragma execution_character_set("utf-8")

using namespace asyncnet;

TEST_CASE("AsyncNetworkSession cancellation") {
	AsyncSession session(1);

	auto worker = [](AsyncSession& session) -> coro::task<void> {
		std::stop_source stop_source;
		auto request = session.make_request<GetRequest>("https://google.com");

		request->set_stop_token(stop_source.get_token());

		stop_source.request_stop();

		// should almost immediately stop
		try {
			co_await session.perform_request(std::move(request));
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

		// should almost immediately stop
		REQUIRE_NOTHROW(co_await session.perform_request(std::move(request)));
	};

	coro::sync_wait(worker(session));
}