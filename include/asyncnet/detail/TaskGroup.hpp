#pragma once
#include <atomic>
#include <coroutine>
#include <cstddef>

namespace asyncnet::detail {

// Borrowed by child promises until completion. The owning gather keeps this
// state at a stable address and retains every child frame through completion.
struct TaskGroup {
    explicit TaskGroup(std::size_t children) : remaining(children + 1) {}

    // One extra arrival belongs to the launcher. Even inline completions must
    // wait until every child has been started before resuming the parent.
    std::atomic<std::size_t> remaining;
    std::coroutine_handle<> continuation;

    bool arrive() noexcept {
        return remaining.fetch_sub(1, std::memory_order_acq_rel) == 1;
    }

    std::coroutine_handle<> complete_child() noexcept {
        return arrive() ? continuation : std::noop_coroutine();
    }
};

} // namespace asyncnet::detail
