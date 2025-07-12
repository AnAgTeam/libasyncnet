#include "catch_amalgamated.hpp"
#include <asyncnet/UrlSession.hpp>
#include <coro/sync_wait.hpp>

#pragma execution_character_set("utf-8")

using namespace asyncnet;

TEST_CASE("AsyncNetworkSession cancellation") {
	AsyncNetworkSession session(1);

	auto worker = [](AsyncNetworkSession& session) -> coro::task<void> {
		std::stop_source stop_source;
		auto session_task = session.get_request("https://google.com", {}, {}, stop_source.get_token());

		stop_source.request_stop();

		// should almost immediately stop
		try {
			std::string output = co_await session_task;
		}
		catch (const NetworkRequestError& net_e) {
			try {
				std::rethrow_if_nested(net_e);
			}
			catch (const curlpp::RuntimeError& e) {
				std::rethrow_exception(std::current_exception());
			}
		}
	};

	coro::sync_wait(worker(session));
}