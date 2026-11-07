#include "catch_amalgamated.hpp"
#ifdef ASYNCNET_TEST_VENDORED_PROGRESS
#include <aniparse/net/vendor/CancellingTask.hpp>
#else
#include <asyncnet/CancellingTask.hpp>
#endif
#include <atomic>
#include <concepts>
#include <latch>
#include <limits>
#include <thread>
#include <utility>

using namespace asyncnet;

static_assert(std::same_as<decltype(std::declval<NetworkPromise<void>&>().read_bytes()), long long>);
static_assert(std::same_as<decltype(std::declval<const NetworkPromise<void>&>().total_bytes()), long long>);

TEST_CASE("Progress counters are independent values including unknown total", "[progress]") {
	NetworkPromise<void> promise;
	CHECK(promise.read_bytes() == 0);
	CHECK(promise.total_bytes() == 0);
	constexpr long long received = 4096;
	promise.set_read_bytes(received);
	CHECK(promise.read_bytes() == received);
	CHECK(promise.total_bytes() == 0);

	constexpr auto large_total = std::numeric_limits<long long>::max();
	promise.set_total_bytes(large_total);
	CHECK(promise.total_bytes() == large_total);
	CHECK(promise.read_bytes() == received);
	promise.set_total_bytes(0);
	CHECK(promise.total_bytes() == 0);
	CHECK(promise.read_bytes() == received);
}

TEST_CASE("Connected promises use the original parent's counters through wrappers", "[progress]") {
	NetworkPromise<void> parent;
	NetworkPromise<int> child;
	NetworkPromise<std::string> grandchild;
	constexpr long long previous_child_progress = 17;
	child.set_read_bytes(previous_child_progress);
	child.connect_with(parent);
	grandchild.connect_with(child);
	CHECK(child.read_bytes() == 0);
	constexpr long long received = 256;
	constexpr long long expected = 1024;
	grandchild.set_read_bytes(received);
	parent.set_total_bytes(expected);
	CHECK(parent.read_bytes() == received);
	CHECK(child.read_bytes() == received);
	CHECK(grandchild.read_bytes() == received);
	CHECK(child.total_bytes() == expected);
	CHECK(grandchild.total_bytes() == expected);
	CHECK(parent.request_stop());
	CHECK(child.stop_requested());
	CHECK(grandchild.stop_requested());
}

TEST_CASE("Progress can be observed while independent writers update connected promises", "[progress][stress]") {
	NetworkPromise<void> parent;
	NetworkPromise<int> child;
	child.connect_with(parent);
	constexpr long long updates = 100000;
	std::latch start(3);
	std::atomic<unsigned> finished{0};
	std::thread read_writer([&] {
		start.arrive_and_wait();
		for (long long value = 1; value <= updates; ++value) child.set_read_bytes(value);
		finished.fetch_add(1, std::memory_order_release);
	});
	std::thread total_writer([&] {
		start.arrive_and_wait();
		for (long long value = 1; value <= updates; ++value) parent.set_total_bytes(value);
		finished.fetch_add(1, std::memory_order_release);
	});
	start.arrive_and_wait();
	bool valid = true;
	long long last_read = 0;
	long long last_total = 0;
	do {
		const auto read = parent.read_bytes();
		const auto total = std::as_const(child).total_bytes();
		// Each counter is coherent independently. There is deliberately no ordering
		// assertion between read and total, which represent different observations.
		valid &= read >= last_read && read <= updates;
		valid &= total >= last_total && total <= updates;
		last_read = read;
		last_total = total;
	} while (finished.load(std::memory_order_acquire) != 2);
	read_writer.join();
	total_writer.join();
	CHECK(valid);
	CHECK(parent.read_bytes() == updates);
	CHECK(child.total_bytes() == updates);
}
