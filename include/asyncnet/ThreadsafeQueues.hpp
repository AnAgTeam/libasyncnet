#pragma once
#include <queue>
#include <mutex>
#include <condition_variable>
#include <optional>
#include <concepts>
#include <coro/mutex.hpp>
#include <coro/condition_variable.hpp>

namespace asyncnet {

	template<typename T, typename Container = std::deque<T>>
	class AsyncQueue {

	public:
		using ValueType = T;
		using Queue = std::queue<T, Container>;

		AsyncQueue() = default;
		template<std::constructible_from<Queue> ... Args>
		explicit AsyncQueue(Args&& ... args) : queue_(std::forward<Args>(args) ...) {}

		// cannot copy or move, because we need to lock the mutex of other queue, but can't co_await here
		AsyncQueue(const AsyncQueue& other) = delete;
		AsyncQueue(AsyncQueue&& other) = delete;
		~AsyncQueue() = default;

		/**
		 * @brief Thread safe pushes value in queue. If there was waiters, execute them
		 * @param value Value to push
		 */
		template<std::convertible_to<ValueType> T>
		coro::task<void> push(T&& value) {
			{
				auto lock = co_await mutex_.scoped_lock();
				queue_.push(std::forward<T>(value));
			}
			co_await cv_.notify_one();
		}

		/**
		 * @brief Thread safe emplace value in queue. If there was waiters, execute them
		 * @param value Value to push
		 */
		template<typename ... Args>
		coro::task<void> emplace(Args&& ... args) {
			{
				auto lock = co_await mutex_.scoped_lock();
				static_cast<void>(queue_.emplace(std::forward<Args>(args) ...));
			}
			co_await cv_.notify_one();
		}

		/**
		 * @brief Thread safe pop queue
		 * @return Returns value if any, otherwise returns std::nullopt
		 */
		coro::task<std::optional<ValueType>> pop() {
			auto lock = co_await mutex_.scoped_lock();
			if (queue_.empty()) {
				co_return std::nullopt;
			}

			auto value = std::move(queue_.front());
			queue_.pop();
			co_return value;
		}

		/**
		 * @brief Thread safe pop queue. It waits for value if there isn't
		 * @return Returns value in queue
		 */
		coro::task<ValueType> pop_wait() {
			auto lock = co_await mutex_.scoped_lock();
			if (queue_.empty()) {
				co_await cv_.wait(lock, [this] {
					return !queue_.empty();
				});
			}

			auto value = std::move(queue_.front());
			queue_.pop();
			co_return value;
		}

	private:

		Queue queue_;
		mutable coro::mutex mutex_;
		coro::condition_variable cv_;
	};
};