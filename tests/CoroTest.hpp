#include "catch_amalgamated.hpp"

#include <coro/task.hpp>
#include <coro/sync_wait.hpp>

#define CORO_TEST_CAT_STR_IMPL(x, y) x##y
#define CORO_TEST_CAT_STR(x, y) CORO_TEST_CAT_STR_IMPL(x, y)

#define CORO_TEST_TEST_UNIQUE_NUMBER __COUNTER__

#define CORO_TEST_TEST_UNIQUE_NAME CORO_TEST_CAT_STR(CORO_INTERNAL_TEST_, CORO_TEST_TEST_UNIQUE_NUMBER)

#define CORO_TEST_CASE_IMPL(name, ...) coro::task<void> name(); \
	TEST_CASE(__VA_ARGS__) {									\
		coro::sync_wait(name());								\
	}															\
	coro::task<void> name()
#define CORO_TEST_CASE(...) CORO_TEST_CASE_IMPL(CORO_TEST_TEST_UNIQUE_NAME, __VA_ARGS__)