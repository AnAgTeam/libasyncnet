#include "CoroTest.hpp"

#include <asyncnet/CancellingTask.hpp>
#include <asyncnet/WhenAll.hpp>
#include <coro/when_all.hpp>
#include <coro/event.hpp>
#include <coro/thread_pool.hpp>

using namespace asyncnet;

static_assert(concepts::derived_from_template<NetworkTask<int>, CancellingTask>);
static_assert(!concepts::derived_from_template<coro::task<int>, CancellingTask>);

template<typename T, typename Arg>
struct DerivedTask;

template<typename T, typename Arg>
struct DerivedPromise : NetworkPromise<T> {
	using coroutine_handle = std::coroutine_handle<DerivedPromise>;

	DerivedTask<T, Arg> get_return_object();
};

template<typename T, typename Arg>
struct DerivedTask : NetworkTask<T, DerivedPromise<T, Arg>> {
	using promise_type = DerivedPromise<T, Arg>;
	using coroutine_handle = std::coroutine_handle<promise_type>;
};

template<typename T, typename Arg>
DerivedTask<T, Arg> DerivedPromise<T, Arg>::get_return_object() {
	return DerivedTask<T, Arg>{ coroutine_handle::from_promise(*this) };
}

CORO_TEST_CASE("CancellingTask cancellation") {
	auto make_autocancel_task = []() -> CancellingTask<void> {
		auto stop_source = co_await awaitables::get_stop_source;
		stop_source.request_stop();
	};

	auto task1 = make_autocancel_task();
	REQUIRE(!task1.get_stop_source().stop_requested());
	co_await task1;
	REQUIRE(task1.get_stop_source().stop_requested());
}

CORO_TEST_CASE("CancellingTask from other coroutine") {
	static auto make_autocancel_task_inner = []() -> CancellingTask<void> {
		auto stop_source = co_await awaitables::get_stop_source;
		stop_source.request_stop();
	};

	auto make_autocancel_task = []() -> CancellingTask<void> {
		co_await make_autocancel_task_inner().update_stop_source(co_await awaitables::get_stop_source);
	};

	auto task1 = make_autocancel_task();
	REQUIRE(!task1.get_stop_source().stop_requested());
	co_await task1;
	REQUIRE(task1.get_stop_source().stop_requested());
}

CORO_TEST_CASE("NetworkTask auto connect") {
	static auto make_autocancel_task_inner = []() -> NetworkTask<void> {
		auto stop_source = co_await awaitables::get_stop_source;
		stop_source.request_stop();
	};

	auto make_autocancel_task = []() -> NetworkTask<void> {
		co_await make_autocancel_task_inner();
	};

	auto task1 = make_autocancel_task();
	REQUIRE(!task1.get_stop_source().stop_requested());
	co_await task1;
	REQUIRE(task1.get_stop_source().stop_requested());
}

CORO_TEST_CASE("Derived NetworkTask auto connect") {
	static auto make_autocancel_task_inner = []() -> DerivedTask<void, int> {
		auto stop_source = co_await awaitables::get_stop_source;
		stop_source.request_stop();
	};

	auto make_autocancel_task = []() -> DerivedTask<void, int> {
		co_await make_autocancel_task_inner();
	};

	auto task1 = make_autocancel_task();
	REQUIRE(!task1.get_stop_source().stop_requested());
	co_await task1;
	REQUIRE(task1.get_stop_source().stop_requested());
}

DerivedTask<void, int> make_autocancel_task_inner(int index = 0) {
	if (index > 0) {
		co_await make_autocancel_task_inner(index - 1);
		co_return;
	}
	auto stop_source = co_await awaitables::get_stop_source;
	stop_source.request_stop();
};

CORO_TEST_CASE("Derived NetworkTask when_all auto connect") {
	static auto noop_task = []() -> coro::task<void> {
		co_return;
	};

	auto make_autocancel_task = []() -> DerivedTask<void, int> {
		co_await when_all(
			co_await awaitables::get_stop_source,
			noop_task(),
			make_autocancel_task_inner(4),
			make_autocancel_task_inner(4)
		);
	};

	auto task1 = make_autocancel_task();
	REQUIRE(!task1.get_stop_source().stop_requested());
	co_await task1;
	REQUIRE(task1.get_stop_source().stop_requested());
}

CORO_TEST_CASE("gather multiple awaitables") {
	auto void_task = []() -> NetworkTask<void> {
		co_return;
	};

	auto make_gather_task = [](std::string value) -> NetworkTask<std::string> {
		co_return std::move(value);
	};

	auto [task0, task1, task2, task3] = co_await gather(
		make_gather_task("hello"),
		make_gather_task("world"),
		make_gather_task("string"),
		void_task()
	);

	REQUIRE(task0 == "hello");
	REQUIRE(task1 == "world");
	REQUIRE(task2 == "string");
}

CORO_TEST_CASE("gather connect and stop") {
	static auto void_task = []() -> NetworkTask<void> {
		co_return;
	};

	auto make_autocancel_task = []() -> DerivedTask<void, int> {
		auto [task0, task1, task2] = co_await gather(
			co_await awaitables::get_stop_source,
			make_autocancel_task_inner(2),
			make_autocancel_task_inner(4),
			void_task()
		);
	};

	auto task1 = make_autocancel_task();
	REQUIRE(!task1.get_stop_source().stop_requested());
	co_await task1;
	REQUIRE(task1.get_stop_source().stop_requested());
}

struct CancelTaskContext {
	coro::event tasks_ready; // from tasks
	coro::event tasks_should_check_cancel; // to tasks
	coro::event tasks_exited; // from tasks
};

DerivedTask<bool, int> make_task_inner(std::shared_ptr<CancelTaskContext> ctx, int index = 0) {
	if (index > 0) {
		co_return co_await make_task_inner(ctx, index - 1);
	}
	ctx->tasks_ready.set();
	co_await ctx->tasks_should_check_cancel;
	co_return (co_await awaitables::get_stop_token).stop_requested();
};

// The thread pool must be owned by the synchronous test scope, not by the
// coroutine below: after `co_await tasks_exited` the coroutine resumes inline on
// a pool worker thread, so destroying the pool from within the coroutine would
// make the pool join its own worker thread (self-join deadlock). Owning it here
// and passing a raw pointer keeps its destruction on the main thread.
static coro::task<void> gather_connect_and_stop_from_outside_impl(coro::thread_pool* thread_pool) {
	auto make_autocancel_task = [](std::shared_ptr<CancelTaskContext> ctx, std::optional<bool>& is_cancelled) -> DerivedTask<void, int> {
		auto [task1, task2] = co_await gather(
			co_await awaitables::get_stop_source,
			make_task_inner(ctx, 2),
			make_task_inner(ctx, 4)
		);
		is_cancelled = task1 && task2;
		ctx->tasks_exited.set();
	};

	auto make_autocancel_helper = [](std::shared_ptr<DerivedTask<void, int>> await_task) -> coro::task<void> {
		co_await *await_task;
	};

	{
		auto tasks_context = std::make_shared<CancelTaskContext>();
		std::optional<bool> is_nocancel_cancelled;
		auto no_cancel_task = std::make_shared<DerivedTask<void, int>>(make_autocancel_task(tasks_context, is_nocancel_cancelled));
		thread_pool->spawn_detached(make_autocancel_helper(no_cancel_task));

		co_await tasks_context->tasks_ready;
		tasks_context->tasks_should_check_cancel.set();
		co_await tasks_context->tasks_exited;

		CHECK(is_nocancel_cancelled.has_value());
		REQUIRE(!(*is_nocancel_cancelled));
	}

	{
		auto tasks_context = std::make_shared<CancelTaskContext>();
		std::optional<bool> is_cancelled;
		auto cancel_task = std::make_shared<DerivedTask<void, int>>(make_autocancel_task(tasks_context, is_cancelled));
		thread_pool->spawn_detached(make_autocancel_helper(cancel_task));

		co_await tasks_context->tasks_ready;
		cancel_task->request_stop();
		tasks_context->tasks_should_check_cancel.set();
		co_await tasks_context->tasks_exited;

		CHECK(is_cancelled.has_value());
		REQUIRE(*is_cancelled);
	}
}

TEST_CASE("gather connect and stop from outside") {
	auto thread_pool = coro::thread_pool::make_unique({
		.thread_count = 1
	});
	coro::sync_wait(gather_connect_and_stop_from_outside_impl(thread_pool.get()));
}