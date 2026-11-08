#pragma once
#include <coroutine>

namespace asyncnet::detail {

// A borrowed completion target. The callback must return a valid suspended
// continuation or noop_coroutine(), never resume it itself. The context must
// remain alive until notification; notification may release its owner.
struct Completion {
    using Notify = std::coroutine_handle<> (*)(void*) noexcept;

    void* context = nullptr;
    Notify notify = [](void*) noexcept -> std::coroutine_handle<> {
        return std::noop_coroutine();
    };

    static Completion continuation(std::coroutine_handle<> handle) noexcept {
        return {handle.address(), [](void* address) noexcept -> std::coroutine_handle<> {
            return address ? std::coroutine_handle<>::from_address(address)
                           : std::noop_coroutine();
        }};
    }
};

} // namespace asyncnet::detail
