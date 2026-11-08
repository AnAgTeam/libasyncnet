#include <asyncnet/Gather.hpp>
#include "catch_amalgamated.hpp"
#include <array>
#include <string>
#include <thread>
#include <vector>

using namespace asyncnet;

namespace {
struct Gate {
    std::coroutine_handle<> waiter;
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> handle) noexcept { waiter = handle; }
    void await_resume() const noexcept {}
    void open() { std::exchange(waiter, {}).resume(); }
};

// Await a borrowed gate while keeping its continuation visible to the test.
struct WaitGate {
    Gate& gate;
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> handle) const noexcept { gate.waiter = handle; }
    void await_resume() const noexcept {}
};

NetworkTask<std::string> text(std::string value) { co_return value; }
NetworkTask<std::unique_ptr<int>> owned(int value) { co_return std::make_unique<int>(value); }
NetworkTask<int&> reference(int& value) { co_return value; }
NetworkTask<const int&> const_reference(const int& value) { co_return value; }
NetworkTask<void> nothing() { co_return; }

struct ImmediateValue {
    bool await_ready() const noexcept { return true; }
    void await_suspend(std::coroutine_handle<>) const noexcept {}
    std::string await_resume() const { return "prvalue"; }
};

struct Immovable {
    Immovable() = default;
    Immovable(const Immovable&) = delete;
    Immovable(Immovable&&) = delete;
};
struct ImmediateImmovable : std::suspend_never {
    Immovable await_resume() const { return {}; }
};

NetworkTask<void> values() {
    auto empty = co_await gather();
    static_assert(std::tuple_size_v<decltype(empty)> == 0);
    int original = 7;
    const int constant = 9;
    auto [value, pointer, ref, cref, placeholder, immediate, immovable] = co_await gather(
        text("owned"), owned(42), reference(original), const_reference(constant),
        nothing(), ImmediateValue{}, ImmediateImmovable{});
    static_assert(std::same_as<decltype(value), std::string>);
    static_assert(std::same_as<decltype(ref), int&>);
    static_assert(std::same_as<decltype(cref), const int&>);
    static_assert(std::same_as<decltype(placeholder), std::monostate>);
    static_assert(std::same_as<decltype(immovable), Immovable>);
    CHECK(value == "owned");
    CHECK(*pointer == 42);
    CHECK(immediate == "prvalue");
    CHECK(&ref == &original);
    CHECK(&cref == &constant);
    ref = 11;
    CHECK(original == 11);

    auto results = co_await gather(text("retained"), reference(original), nothing());
    auto moved = std::move(results);
    static_assert(std::same_as<decltype(get<0>(moved)), std::string&>);
    static_assert(std::same_as<decltype(get<0>(std::as_const(moved))), const std::string&>);
    static_assert(std::same_as<decltype(get<1>(std::as_const(moved))), int&>);
    static_assert(std::same_as<decltype(get<1>(std::move(moved))), int&>);
    CHECK(get<0>(moved) == "retained");
    CHECK(&get<1>(moved) == &original);
    auto extracted = get<0>(std::move(moved));
    CHECK(extracted == "retained");
    auto& [stored, borrowed, empty_value] = moved;
    CHECK(&borrowed == &original);
}

template<typename T>
void finish(T& task) {
    task.resume();
    REQUIRE(task.done());
    task.promise().result();
}

NetworkTask<bool> observe(Gate& gate, bool& callback_called) {
    auto token = co_await awaitables::get_stop_token;
    std::stop_callback callback(token, [&] { callback_called = true; });
    co_await WaitGate{gate};
    co_return token.stop_requested();
}

NetworkTask<bool> nested_observe(Gate& gate, bool& callback_called) {
    auto [stopped] = co_await gather(observe(gate, callback_called));
    co_return stopped;
}

NetworkTask<void> cancellation_parent(std::array<Gate, 3>& gates, std::array<bool, 2>& callbacks,
                                      std::array<bool, 2>& results) {
    auto [first, nested, plain] = co_await gather(
        observe(gates[0], callbacks[0]), nested_observe(gates[1], callbacks[1]), WaitGate{gates[2]});
    results = {first, nested};
}

NetworkTask<bool> check_stop() { co_return (co_await awaitables::get_stop_token).stop_requested(); }
CancellingTask<bool> basic_check_stop() { co_return (co_await awaitables::get_stop_token).stop_requested(); }
NetworkTask<void> stop_group() { (co_await awaitables::get_stop_source).request_stop(); }

NetworkTask<void> stopped_before_start() {
    auto [network, basic, nested] = co_await gather(check_stop(), basic_check_stop(), gather(check_stop()));
    CHECK(network);
    CHECK(basic);
    CHECK(get<0>(nested));
}

CancellingTask<void> basic_parent() {
    auto [network, basic] = co_await gather(check_stop(), basic_check_stop());
    CHECK(network);
    CHECK(basic);
}

NetworkTask<void> sibling_stop() {
    auto [first, second] = co_await gather(stop_group(), check_stop());
    CHECK(second);
}

NetworkTask<void> explicit_source(std::stop_source source, bool expected) {
    auto [network, basic, nested] = co_await gather(source, check_stop(), basic_check_stop(), gather(check_stop()));
    CHECK(network == expected);
    CHECK(basic == expected);
    CHECK(get<0>(nested) == expected);
}

NetworkTask<int> fail() {
    throw std::runtime_error("gather child failed");
    co_return 0;
}
NetworkTask<void> fail_void() {
    throw std::runtime_error("void child failed");
    co_return;
}
NetworkTask<void> exception_parent(Gate& gate, bool& caught) {
    try {
        co_await gather(fail(), WaitGate{gate}, fail_void());
        FAIL("Discarding gather results must still throw");
    } catch (const std::runtime_error& error) {
        CHECK(std::string(error.what()) == "gather child failed");
        caught = true;
    }
}

NetworkTask<void> discarded_void_exception() {
    co_await gather(nothing(), fail_void());
}

NetworkTask<int> deferred_fail(Gate& gate) {
    co_await WaitGate{gate};
    throw std::runtime_error("first argument failed last");
    co_return 0;
}
NetworkTask<void> exception_order(Gate& gate) {
    co_await gather(deferred_fail(gate), fail());
}

NetworkTask<void> binding_exception() {
    auto [value, empty] = co_await gather(fail(), nothing());
}

constexpr long long progress_read = 37;
constexpr long long progress_total = 91;
NetworkTask<void> publish_progress() {
    auto& promise = co_await detail::get_promise;
    promise.set_read_bytes(progress_read);
    promise.set_total_bytes(progress_total);
}
NetworkTask<void> progress_parent() {
    co_await gather(gather(publish_progress()), nothing());
}

struct ThreadHop {
    std::vector<std::thread>& threads;
    bool await_ready() const noexcept { return false; }
    void await_suspend(std::coroutine_handle<> handle) const {
        threads.emplace_back([handle] { handle.resume(); });
    }
    void await_resume() const noexcept {}
};
NetworkTask<int> threaded(std::vector<std::thread>& threads, int value) {
    co_await ThreadHop{threads};
    co_return value;
}
NetworkTask<void> threaded_parent(std::vector<std::thread>& threads, int& sum) {
    auto [a, b, c] = co_await gather(threaded(threads, 1), threaded(threads, 2), threaded(threads, 3));
    sum = a + b + c;
}
}

TEST_CASE("Gather preserves values, references, void and move-only storage", "[gather]") {
    auto task = values();
    finish(task);
}

TEST_CASE("Gather cancellation reaches nested tasks but still waits for every awaitable", "[gather]") {
    std::array<Gate, 3> gates;
    std::array<bool, 2> callbacks{};
    std::array<bool, 2> results{};
    auto task = cancellation_parent(gates, callbacks, results);
    task.resume();
    REQUIRE_FALSE(task.done());
    REQUIRE(gates[0].waiter);
    REQUIRE(gates[1].waiter);
    REQUIRE(gates[2].waiter);
    std::thread canceller([stop = task.get_stop_source()]() mutable { stop.request_stop(); });
    canceller.join();
    CHECK(task.get_stop_source().stop_requested());
    CHECK(callbacks[0]);
    CHECK(callbacks[1]);
    gates[1].open();
    CHECK_FALSE(task.done());
    gates[0].open();
    CHECK_FALSE(task.done());
    gates[2].open();
    REQUIRE(task.done());
    task.promise().result();
    CHECK(results[0]);
    CHECK(results[1]);
}

TEST_CASE("Gather inherits pre-start cancellation including basic and nested groups", "[gather]") {
    auto network = stopped_before_start();
    network.request_stop();
    finish(network);
    auto basic = basic_parent();
    basic.request_stop();
    finish(basic);
}

TEST_CASE("Gather shares child cancellation with siblings and parent", "[gather]") {
    auto task = sibling_stop();
    finish(task);
    CHECK(task.get_stop_source().stop_requested());
}

TEST_CASE("Explicit gather source overrides parent cancellation", "[gather]") {
    std::stop_source source;
    auto task = explicit_source(source, false);
    task.request_stop();
    finish(task);
    CHECK_FALSE(source.stop_requested());
    source.request_stop();
    auto stopped = explicit_source(source, true);
    finish(stopped);
    CHECK_FALSE(stopped.get_stop_source().stop_requested());
}

TEST_CASE("Gather joins siblings before throwing even when results are discarded", "[gather]") {
    Gate gate;
    bool caught = false;
    auto task = exception_parent(gate, caught);
    task.resume();
    CHECK_FALSE(task.done());
    CHECK_FALSE(caught);
    gate.open();
    REQUIRE(task.done());
    task.promise().result();
    CHECK(caught);
    auto binding = binding_exception();
    binding.resume();
    REQUIRE(binding.done());
    CHECK_THROWS_WITH(binding.promise().result(), "gather child failed");
    auto discarded = discarded_void_exception();
    discarded.resume();
    REQUIRE(discarded.done());
    CHECK_THROWS_WITH(discarded.promise().result(), "void child failed");
}

TEST_CASE("Gather chooses exceptions in argument order, not completion order", "[gather]") {
    Gate gate;
    auto task = exception_order(gate);
    task.resume();
    REQUIRE_FALSE(task.done());
    gate.open();
    REQUIRE(task.done());
    CHECK_THROWS_WITH(task.promise().result(), "first argument failed last");
}

TEST_CASE("Gather preserves shared network progress through nested groups", "[gather]") {
    auto task = progress_parent();
    finish(task);
    CHECK(task.read_bytes() == progress_read);
    CHECK(task.total_bytes() == progress_total);
}

TEST_CASE("Gather safely joins concurrent and inline completions", "[gather][stress]") {
    constexpr int repetitions = 200;
    for (int iteration = 0; iteration < repetitions; ++iteration) {
        std::vector<std::thread> threads;
        int sum = 0;
        auto task = threaded_parent(threads, sum);
        // CancellingTask::resume() inspects done() after resuming, which races
        // with completion on a worker. Launch without that unsynchronized read.
        task.handle().resume();
        for (auto& thread : threads) thread.join();
        REQUIRE(task.done());
        task.promise().result();
        CHECK(sum == 6);
    }
}

namespace {
struct Tracked {
    int& live;
    explicit Tracked(int& count) : live(count) { ++live; }
    Tracked(const Tracked&) = delete;
    Tracked(Tracked&&) = delete;
    ~Tracked() { --live; }
};
struct TrackedValue : std::suspend_never {
    int& live;
    Tracked await_resume() const { return Tracked{live}; }
};
using RetainedResults = decltype(gather(std::declval<TrackedValue>()).await_resume());
NetworkTask<RetainedResults> retained_results(int& live) {
    co_return co_await gather(TrackedValue{{}, live});
}

template<typename T> struct DerivedTask;
template<typename T> struct DerivedPromise : NetworkPromise<T> {
    DerivedTask<T> get_return_object();
};
template<typename T> struct DerivedTask : NetworkTask<T, DerivedPromise<T>> {};
template<typename T>
DerivedTask<T> DerivedPromise<T>::get_return_object() {
    return DerivedTask<T>{std::coroutine_handle<DerivedPromise<T>>::from_promise(*this)};
}
DerivedTask<bool> derived_child() { co_return (co_await awaitables::get_stop_token).stop_requested(); }
DerivedTask<void> derived_parent() {
    auto [child, network, basic] = co_await gather(derived_child(), check_stop(), basic_check_stop());
    CHECK(child);
    CHECK(network);
    CHECK(basic);
}

// A parent with no asyncnet promise, using the same wrapper machinery as any
// foreign coroutine: only the explicitly supplied source can connect children.
struct ForeignTask {
    struct promise_type {
        ForeignTask get_return_object() { return {std::coroutine_handle<promise_type>::from_promise(*this)}; }
        std::suspend_always initial_suspend() noexcept { return {}; }
        std::suspend_always final_suspend() noexcept { return {}; }
        void return_void() noexcept {}
        void unhandled_exception() { throw; }
    };
    std::coroutine_handle<promise_type> handle;
    ~ForeignTask() { handle.destroy(); }
};
ForeignTask foreign_parent(std::stop_source stop) {
    auto [plain, cancelled] = co_await gather(check_stop(), gather(stop, check_stop()));
    CHECK_FALSE(plain);
    CHECK(get<0>(cancelled));
}

struct RefAwaiter {
    RefAwaiter() = default;
    RefAwaiter(const RefAwaiter&) = delete;
    bool await_ready() const noexcept { return true; }
    void await_suspend(std::coroutine_handle<>) const noexcept {}
    int await_resume() & noexcept { return 5; }
};
struct RefAwaitable {
    RefAwaiter& awaiter;
    RefAwaiter& operator co_await() && noexcept { return awaiter; }
};
struct FreeAwaitable {};
ImmediateValue operator co_await(FreeAwaitable) { return {}; }
NetworkTask<void> custom_awaiters(RefAwaiter& awaiter) {
    auto [ref, free] = co_await gather(RefAwaitable{awaiter}, FreeAwaitable{});
    CHECK(ref == 5);
    CHECK(free == "prvalue");
}
}

TEST_CASE("Gather result ownership outlives its awaiting coroutine", "[gather][lifetime]") {
    int live = 0;
    std::optional<RetainedResults> results;
    {
        auto task = retained_results(live);
        task.resume();
        REQUIRE(task.done());
        results.emplace(std::move(task.promise()).result());
        CHECK(live == 1);
    }
    CHECK(live == 1);
    CHECK(&get<0>(*results).live == &live);
    results.reset();
    CHECK(live == 0);
}

TEST_CASE("Gather automatically connects derived task and promise types", "[gather]") {
    auto task = derived_parent();
    task.request_stop();
    finish(task);
}

TEST_CASE("Gather supports foreign parents with optional explicit cancellation", "[gather]") {
    std::stop_source stop;
    stop.request_stop();
    auto task = foreign_parent(stop);
    task.handle.resume();
    REQUIRE(task.handle.done());
}

TEST_CASE("Gather supports ADL co_await and noncopyable lvalue awaiters", "[gather]") {
    RefAwaiter awaiter;
    auto task = custom_awaiters(awaiter);
    finish(task);
}

namespace {
struct Transfers {
    int copies = 0;
    int moves = 0;
    int live = 0;
};
struct Counted {
    Transfers& counts;
    explicit Counted(Transfers& counts) : counts(counts) { ++counts.live; }
    Counted(const Counted& other) : counts(other.counts) { ++counts.copies; ++counts.live; }
    Counted(Counted&& other) noexcept : counts(other.counts) { ++counts.moves; ++counts.live; }
    ~Counted() { --counts.live; }
};
NetworkTask<Counted> counted_task(Transfers& counts) { co_return Counted{counts}; }
NetworkTask<Counted&> counted_reference(Counted& value) { co_return value; }
NetworkTask<const Counted&> counted_const_reference(const Counted& value) { co_return value; }
struct CountedPrvalue : std::suspend_never {
    Transfers& counts;
    Counted await_resume() const { return Counted{counts}; }
};

NetworkTask<void> direct_counted_result(Transfers& counts) {
    auto value = co_await counted_task(counts);
    CHECK(counts.copies == 0);
    CHECK(counts.moves == 2); // Into the task promise, then into value.
    CHECK(counts.live == 1);
}

NetworkTask<void> gathered_counted_results(Transfers& first, Transfers& second) {
    auto [a, b] = co_await gather(counted_task(first), counted_task(second));
    CHECK(first.copies == 0);
    CHECK(second.copies == 0);
    CHECK(first.moves == 1); // Into each task promise only.
    CHECK(second.moves == 1);
    CHECK(first.live == 1);
    CHECK(second.live == 1);
    CHECK(&a.counts == &first);
    CHECK(&b.counts == &second);
    auto own = std::move(a);
    CHECK(first.moves == 2);
    CHECK(first.copies == 0);
    CHECK(first.live == 2); // The moved-from promise value is still alive.
}

NetworkTask<void> counted_storage(Transfers& counts) {
    auto results = co_await gather(counted_task(counts));
    CHECK(counts.moves == 1);
    auto* address = &get<0>(results);
    auto moved = std::move(results);
    CHECK(&get<0>(moved) == address);
    auto& [a] = moved;
    const auto& [b] = moved;
    CHECK(&a == &b);
    CHECK(&a == address);
    CHECK(counts.moves == 1);
    CHECK(counts.copies == 0);
    auto copy = get<0>(moved);
    CHECK(counts.copies == 1);
    CHECK(counts.moves == 1);
    auto extracted = get<0>(std::move(moved));
    CHECK(counts.copies == 1);
    CHECK(counts.moves == 2);
}

NetworkTask<void> counted_references(Counted& original) {
    auto [a, b] = co_await gather(counted_reference(original), counted_const_reference(original));
    static_assert(std::same_as<decltype(a), Counted&>);
    static_assert(std::same_as<decltype(b), const Counted&>);
    CHECK(&a == &original);
    CHECK(&b == &original);
    CHECK(original.counts.copies == 0);
    CHECK(original.counts.moves == 0);
    CHECK(original.counts.live == 1);
}

NetworkTask<void> counted_prvalue(Transfers& counts) {
    auto [a] = co_await gather(CountedPrvalue{{}, counts});
    CHECK(&a.counts == &counts);
    CHECK(counts.moves == 0);
    CHECK(counts.copies == 0);
    CHECK(counts.live == 1);
}
}

TEST_CASE("Task result transfer baseline distinguishes promise storage and extraction", "[gather][transfers]") {
    Transfers counts;
    {
        auto task = counted_task(counts);
        task.resume();
        REQUIRE(task.done());
        CHECK(counts.moves == 1);
        CHECK(counts.copies == 0);
        CHECK(counts.live == 1);
    }
    CHECK(counts.live == 0);
    counts = {};
    auto direct = direct_counted_result(counts);
    finish(direct);
    CHECK(counts.live == 0);
}

TEST_CASE("Gather structured binding adds no result moves or copies", "[gather][transfers]") {
    Transfers first;
    Transfers second;
    auto task = gathered_counted_results(first, second);
    finish(task);
    CHECK(first.live == 0);
    CHECK(second.live == 0);
}

TEST_CASE("Moving gather storage preserves result identity without transferring values", "[gather][transfers]") {
    Transfers counts;
    auto task = counted_storage(counts);
    finish(task);
    CHECK(counts.live == 0);
}

TEST_CASE("Gather reference results are never copied or moved", "[gather][transfers]") {
    Transfers counts;
    {
        Counted original{counts};
        auto task = counted_references(original);
        finish(task);
        CHECK(counts.live == 1);
    }
    CHECK(counts.live == 0);
}

TEST_CASE("Gather retains prvalue awaitable results without any value transfers", "[gather][transfers]") {
    Transfers counts;
    auto task = counted_prvalue(counts);
    finish(task);
    CHECK(counts.live == 0);
}

namespace {
struct ObservedTask : NetworkTask<int> {
    ObservedTask(NetworkTask<int> task, int& observations)
        : NetworkTask<int>(std::move(task)), observations(observations) {}
    int& observations;
    auto operator co_await() const&& noexcept {
        ++observations;
        return std::move(static_cast<const NetworkTask<int>&>(*this)).operator co_await();
    }
};
NetworkTask<int> integer_value() { co_return 42; }
NetworkTask<void> custom_task_awaiter(int& observations) {
    auto [value, plain] = co_await gather(ObservedTask{integer_value(), observations}, integer_value());
    CHECK(value == 42);
    CHECK(plain == 42);
}

NetworkTask<void> completed_inputs() {
    auto value = integer_value();
    auto empty = nothing();
    auto failed = fail();
    value.resume();
    empty.resume();
    failed.resume();
    REQUIRE(value.done());
    REQUIRE(empty.done());
    REQUIRE(failed.done());
    auto [answer, placeholder, unstarted] = co_await gather(std::move(value), std::move(empty), integer_value());
    CHECK(answer == 42);
    CHECK(unstarted == 42);
    bool caught = false;
    try {
        co_await gather(std::move(failed), nothing());
    } catch (const std::runtime_error& error) {
        caught = true;
        CHECK(std::string(error.what()) == "gather child failed");
    }
    CHECK(caught);
}
}

TEST_CASE("Gather keeps custom task awaiters on the generic path", "[gather]") {
    int observations = 0;
    auto task = custom_task_awaiter(observations);
    finish(task);
    CHECK(observations == 1);
}

TEST_CASE("Gather does not resume tasks that have already completed", "[gather]") {
    auto task = completed_inputs();
    finish(task);
}

TEST_CASE("Task completion returns a continuation and can be replaced", "[task][completion]") {
    struct Target {
        std::coroutine_handle<> continuation;
        int notifications = 0;
    };
    auto receiver = nothing();
    auto child = nothing();
    Target target{receiver.handle()};
    child.promise().completion({&target, [](void* context) noexcept {
        auto& target = *static_cast<Target*>(context);
        ++target.notifications;
        return target.continuation;
    }});

    SECTION("Completion transfers to the returned coroutine") {
        child.handle().resume();
        CHECK(target.notifications == 1);
        CHECK(child.handle().done());
        CHECK(receiver.handle().done());
    }
    SECTION("Ordinary continuation replaces the callback") {
        child.promise().continuation(receiver.handle());
        child.handle().resume();
        CHECK(target.notifications == 0);
        CHECK(child.handle().done());
        CHECK(receiver.handle().done());
    }
}
