#include <asyncnet/Gather.hpp>
#include <cstdio>
#include <cstdlib>
#include <new>

// A separate executable keeps allocation accounting independent of Catch2 and
// other tests. Count only group construction; input task frames already exist.
namespace {
thread_local bool count_allocations = false;
thread_local std::size_t allocations = 0;
}

void* operator new(std::size_t size) {
    if (count_allocations) ++allocations;
    // A successful zero-size allocation must still return a non-null pointer.
    if (void* pointer = std::malloc(size == 0 ? 1 : size)) return pointer;
    throw std::bad_alloc();
}
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { std::free(pointer); }
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { std::free(pointer); }

namespace {
asyncnet::NetworkTask<int> value(int index) { co_return index; }

template<typename Group, std::size_t... Index>
asyncnet::NetworkTask<bool> consume(Group group, std::index_sequence<Index...>) {
    auto results = co_await std::move(group);
    co_return ((asyncnet::get<Index>(results) == static_cast<int>(Index)) && ...);
}

template<std::size_t... Index>
bool check_allocations(std::index_sequence<Index...> indices) {
    auto inputs = std::tuple{value(static_cast<int>(Index))...};
    allocations = 0;
    count_allocations = true;
    auto group = std::apply([](auto&... tasks) {
        return asyncnet::gather(std::move(tasks)...);
    }, inputs);
    count_allocations = false;
    const auto group_allocations = allocations;
    auto task = consume(std::move(group), indices);
    task.handle().resume();
    const bool results_ok = task.done() && task.promise().result();
    std::printf("%zu tasks: %zu gather allocations; results %s\n",
                sizeof...(Index), group_allocations, results_ok ? "ok" : "FAILED");
    // One stable group allocation, no extra coroutine frames. Permit compilers
    // to elide allocations; adding a wrapper per input must exceed this budget.
    constexpr std::size_t group_allocation_budget = 1;
    return results_ok && group_allocations <= group_allocation_budget;
}
}

int main() {
    bool passed = check_allocations(std::make_index_sequence<1>{});
    passed = check_allocations(std::make_index_sequence<4>{}) && passed;
    passed = check_allocations(std::make_index_sequence<16>{}) && passed;
    return passed ? EXIT_SUCCESS : EXIT_FAILURE;
}
