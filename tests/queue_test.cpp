#include "catch_amalgamated.hpp"
#include <asyncnet/ThreadsafeQueues.hpp>
#include <coro/sync_wait.hpp>

#pragma execution_character_set("utf-8")

using namespace asyncnet;

TEST_CASE("AsyncQueue construct from variable templated") {
	AsyncQueue<int> a_queue(std::queue<int>({ 1, 3 }));
}

TEST_CASE("AsyncQueue push pop emplace") {
	AsyncQueue<int> queue;

	auto work_function = [&queue]() -> coro::task<void> {
		auto val_before_push = co_await queue.pop();
		REQUIRE_FALSE(val_before_push.has_value());

		co_await queue.push(10);

		auto val_after_push = co_await queue.pop();
		REQUIRE(val_after_push.has_value());
		REQUIRE(val_after_push.value() == 10);

		auto val_after_pop = co_await queue.pop();
		REQUIRE_FALSE(val_after_pop.has_value());

		co_await queue.emplace(100);

		auto val_after_emplace = co_await queue.pop();
		REQUIRE(val_after_emplace.has_value());
		REQUIRE(val_after_emplace.value() == 100);
	};

	coro::sync_wait(work_function());
}

TEST_CASE("AsyncQueue push pop_wait") {
	AsyncQueue<int> queue;

	auto orig_thread_id = std::this_thread::get_id();

	auto work_function = [](AsyncQueue<int>& queue, std::thread::id orig_thread_id, int id) -> coro::task<void> {
		if (id == 2) {
			co_await queue.push(id);
			REQUIRE(orig_thread_id == std::this_thread::get_id());
		}

		auto val = co_await queue.pop_wait();
		REQUIRE(orig_thread_id == std::this_thread::get_id());
		REQUIRE((id == 1 ? val == 2 : val == 1));

		if (id == 1) {
			co_await queue.push(id);
			REQUIRE(orig_thread_id == std::this_thread::get_id());
		}
	};

	coro::sync_wait(when_all(work_function(queue, orig_thread_id, 1), work_function(queue, orig_thread_id, 2)));
}
