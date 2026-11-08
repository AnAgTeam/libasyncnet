#pragma once
#include <asyncnet/CancellingTask.hpp>
#include <cassert>
#include <optional>
#include <tuple>

namespace asyncnet {
namespace detail {

// Preserve the awaiter's value category, including non-copyable lvalue awaiters.
template<typename T>
decltype(auto) gather_awaiter(T&& value) {
    if constexpr (requires { std::forward<T>(value).operator co_await(); }) {
        return std::forward<T>(value).operator co_await();
    } else if constexpr (requires { operator co_await(std::forward<T>(value)); }) {
        return operator co_await(std::forward<T>(value));
    } else {
        return std::forward<T>(value);
    }
}

template<typename T>
using GatherReturn = decltype(std::declval<decltype(gather_awaiter(std::declval<T&&>()))&>().await_resume());

template<typename T>
concept GatherInput = requires(T&& value, std::coroutine_handle<> continuation) {
    { gather_awaiter(std::forward<T>(value)).await_ready() } -> std::convertible_to<bool>;
    gather_awaiter(std::forward<T>(value)).await_suspend(continuation);
    gather_awaiter(std::forward<T>(value)).await_resume();
};

struct GatherContext {
    std::optional<TaskContext> network;
    std::optional<std::stop_source> stop;
};

template<typename T>
void connect_gather_input(T& input, const GatherContext& context) {
    if constexpr (concepts::derived_from_template<T, NetworkTask>) {
        if (context.network) input.connect_with(*context.network);
    }
    if constexpr (concepts::derived_from_template<T, CancellingTask>) {
        if (context.stop) input.update_stop_source(*context.stop);
    } else if constexpr (requires { input.inherit_context(context); }) {
        input.inherit_context(context);
    }
}

struct GatherLatch {
    explicit GatherLatch(std::size_t children) : remaining(children + 1) {}

    // The extra arrival belongs to await_suspend. Children cannot resume the
    // parent until every child has been started and the continuation is set.
    std::atomic<std::size_t> remaining;
    std::coroutine_handle<> continuation;

    bool arrive() noexcept {
        return remaining.fetch_sub(1, std::memory_order_acq_rel) == 1;
    }
};

struct GetGatherContext {};

template<typename T>
class GatherChild {
public:
    struct promise_type {
        GatherLatch* latch = nullptr;
        GatherContext context;
        std::remove_reference_t<T>* value = nullptr;
        std::exception_ptr exception;

        GatherChild get_return_object() noexcept {
            return GatherChild{std::coroutine_handle<promise_type>::from_promise(*this)};
        }
        std::suspend_always initial_suspend() const noexcept { return {}; }

        struct Completion {
            bool await_ready() const noexcept { return false; }
            std::coroutine_handle<> await_suspend(std::coroutine_handle<promise_type> handle) const noexcept {
                auto* latch = handle.promise().latch;
                // No access to the frame after handing control back to its owner.
                return latch->arrive() ? latch->continuation : std::noop_coroutine();
            }
            void await_resume() const noexcept {}
        };

        Completion final_suspend() const noexcept { return {}; }
        void unhandled_exception() noexcept { exception = std::current_exception(); }
        void return_void() const noexcept {}
        Completion yield_value(T&& result) noexcept {
            value = std::addressof(result);
            return {};
        }
        auto await_transform(GetGatherContext) noexcept {
            struct Awaiter : std::suspend_never {
                const GatherContext& context;
                const GatherContext& await_resume() const noexcept { return context; }
            };
            return Awaiter{{}, context};
        }
        template<typename A>
        A&& await_transform(A&& input) noexcept { return std::forward<A>(input); }
    };

    using Handle = std::coroutine_handle<promise_type>;
    explicit GatherChild(Handle handle) noexcept : handle_(handle) {}
    GatherChild(const GatherChild&) = delete;
    GatherChild(GatherChild&& other) noexcept : handle_(std::exchange(other.handle_, {})) {}
    ~GatherChild() { if (handle_) handle_.destroy(); }

    void start(GatherLatch& latch, const GatherContext& context) {
        auto& promise = handle_.promise();
        promise.latch = &latch;
        promise.context = context;
        handle_.resume();
    }

    T& result() & {
        rethrow_if_failed();
        return *handle_.promise().value;
    }
    using Value = std::conditional_t<std::is_lvalue_reference_v<T>, T, std::remove_reference_t<T>>;
    const Value& result() const& {
        rethrow_if_failed();
        return *handle_.promise().value;
    }
    const Value&& result() const&& {
        rethrow_if_failed();
        return static_cast<const Value&&>(*handle_.promise().value);
    }
    T&& result() && {
        rethrow_if_failed();
        return std::forward<T>(*handle_.promise().value);
    }
    void rethrow_if_failed() const {
        if (handle_.promise().exception) std::rethrow_exception(handle_.promise().exception);
    }
private:
    Handle handle_;
};

template<typename A>
using GatherValue = std::conditional_t<std::is_void_v<GatherReturn<A>>, std::monostate, GatherReturn<A>>;

template<GatherInput A>
GatherChild<GatherValue<A>> make_gather_child(A input) {
    const auto& context = co_await GetGatherContext{};
    connect_gather_input(input, context);
    if constexpr (std::is_void_v<GatherReturn<A>>) {
        co_await std::move(input);
        co_yield std::monostate{};
    } else {
        // Keep prvalues and the awaited task alive at this suspension point.
        // Explicit reference results keep borrowing their original referent.
        co_yield co_await std::move(input);
    }
}

template<typename... A>
struct GatherState : GatherLatch {
    explicit GatherState(A&&... inputs)
        : GatherLatch(sizeof...(A)), children(make_gather_child(std::move(inputs))...) {}
    std::tuple<GatherChild<GatherValue<A>>...> children;
};

template<typename... A>
struct GatherResults {
    // Stable storage survives moving the result, including coroutine completion
    // on another thread. Destroy only after all children have finished.
    std::unique_ptr<GatherState<A...>> state;
};

template<std::size_t Index, typename... A>
decltype(auto) get(GatherResults<A...>& results) {
    return std::get<Index>(results.state->children).result();
}
template<std::size_t Index, typename... A>
decltype(auto) get(const GatherResults<A...>& results) {
    return std::as_const(std::get<Index>(results.state->children)).result();
}
template<std::size_t Index, typename... A>
decltype(auto) get(GatherResults<A...>&& results) {
    return std::move(std::get<Index>(results.state->children)).result();
}

template<std::size_t Index, typename... A>
decltype(auto) get(const GatherResults<A...>&& results) {
    return std::move(std::as_const(std::get<Index>(results.state->children))).result();
}

template<typename... A>
class GatherAwaitable {
public:
    explicit GatherAwaitable(std::optional<std::stop_source> stop, A&&... inputs)
        : state_(std::make_unique<GatherState<A...>>(std::move(inputs)...)), explicit_stop_(std::move(stop)) {}

    void inherit_context(const GatherContext& context) { context_ = context; }
    bool await_ready() const noexcept { return sizeof...(A) == 0; }

    template<typename P>
    bool await_suspend(std::coroutine_handle<P> parent) {
        assert(state_ && !started_);
        started_ = true;
        if constexpr (std::derived_from<P, BasicPromise>) {
            context_.stop = parent.promise().stop_source();
        }
        if constexpr (concepts::derived_from_template<P, NetworkPromise>) {
            context_.network = parent.promise().await_transform(GetSelfTag{}).await_resume();
        }
        // An explicitly supplied source overrides automatic cancellation only.
        if (explicit_stop_) context_.stop = explicit_stop_;
        state_->continuation = parent;
        std::apply([&](auto&... children) { (children.start(*state_, context_), ...); }, state_->children);
        return !state_->arrive();
    }

    auto await_resume() {
        assert(state_);
        // Check in argument order even if the caller discards the results.
        // All children have completed before any exception leaves the group.
        std::apply([](const auto&... children) { (children.rethrow_if_failed(), ...); }, state_->children);
        return GatherResults<A...>{std::move(state_)};
    }
private:
    std::unique_ptr<GatherState<A...>> state_;
    std::optional<std::stop_source> explicit_stop_;
    GatherContext context_;
    bool started_ = false;
};

} // namespace detail

using detail::get;

/**
 * @brief Start an owned group of awaitables and wait for every child to finish.
 * @param inputs Unstarted awaitables, moved into the group.
 * @return A single-use awaitable yielding tuple-like results in argument order.
 * @note CancellingTask children inherit the awaiting task's stop state. NetworkTask
 *       children also share a NetworkTask parent's progress counters (not a sum).
 *       Arbitrary awaitables have no automatic cancellation support. Cancellation
 *       is cooperative and never skips waiting for any child to finish.
 * @throws Rethrows the first child exception in argument order after every child
 *         finishes, including when the caller discards the results.
 * @note Results own the child frames. Values and void placeholders (std::monostate)
 *       live in those frames; lvalue references preserve their original identity.
 *       Keep the results alive while using references into them. Borrowed inputs,
 *       coroutine receivers and external reference results must outlive their use.
 *       Do not destroy or move an actively awaited group or its awaiting task.
 */
template<detail::GatherInput... A>
[[nodiscard]] auto gather(A... inputs) {
    return detail::GatherAwaitable<A...>{std::nullopt, std::move(inputs)...};
}

/**
 * @brief Gather awaitables using an explicit cancellation source.
 * @param stop Source shared with cancellable children, overriding the parent's.
 * @param inputs Unstarted awaitables, moved into the group.
 * @return A single-use awaitable with the same result and lifetime contract as gather().
 * @note Useful outside an asyncnet task. Network progress still follows the parent
 *       when present. Existing child cancellation sources are replaced before start.
 */
template<detail::GatherInput... A>
[[nodiscard]] auto gather(const std::stop_source& stop, A... inputs) {
    return detail::GatherAwaitable<A...>{stop, std::move(inputs)...};
}

} // namespace asyncnet

template<typename... A>
struct std::tuple_size<asyncnet::detail::GatherResults<A...>>
    : std::integral_constant<std::size_t, sizeof...(A)> {};

template<std::size_t Index, typename... A>
struct std::tuple_element<Index, asyncnet::detail::GatherResults<A...>> {
    using Result = asyncnet::detail::GatherValue<std::tuple_element_t<Index, std::tuple<A...>>>;
    using type = std::conditional_t<std::is_rvalue_reference_v<Result>, std::remove_reference_t<Result>, Result>;
};
