#pragma once
#include <asyncnet/utility/Concepts.hpp>
#include <coro/when_all.hpp>

namespace asyncnet {
	template<typename T>
	class Promise;

	template<typename T, std::derived_from<Promise<T>> TaskPromise>
	class CancellingTask;

	namespace detail {
		template<coro::concepts::awaitable T>
		void try_connect(T&, const std::stop_source&) {

		}

		template<coro::concepts::awaitable T>
			requires concepts::derived_from_template<T, CancellingTask>
		void try_connect(T& awaitable, const std::stop_source& src) {
			awaitable.update_stop_source(src);
		}

		/// coro::when_all awaitable, that can be co_await'ed
		template<coro::concepts::awaitable ... Awaitables>
		using WhenAllAwaitable = decltype(coro::when_all(std::declval<Awaitables>() ...));

		/// Tuple with return values from coro::when_all's task
		template<coro::concepts::awaitable ... Awaitables>
		using WhenAllAwaitableTuple = std::remove_reference_t<typename coro::concepts::awaitable_traits<WhenAllAwaitable<Awaitables ...>>::awaiter_return_type>;

		/**
		 * Class that holds coro::when_all tasks alive
		 * and acts as std::tuple
		 * @tparam Awaitables ... Awaitable types, that all will be co_await'ed
		 */
		template<coro::concepts::awaitable ... Awaitables>
		struct GatherTasksStorage {
			/**
			 * @brief construct storage
			 * @param task coro::when_all task
			 */
			GatherTasksStorage(WhenAllAwaitable<Awaitables ...>&& task) : when_all_task(std::move(task)) {}

			/// disable copy for safety
			GatherTasksStorage(GatherTasksStorage& other) = delete;
			GatherTasksStorage(GatherTasksStorage&& other) = default;

			/// disable copy for safety
			GatherTasksStorage& operator=(GatherTasksStorage& other) = delete;
			/// disable move for safety
			GatherTasksStorage& operator=(GatherTasksStorage&& other) = delete;

			WhenAllAwaitable<Awaitables ...> when_all_task;
		};

		/**
		 * Helper class to fix behavior when std::get
		 * are called after the task if destroyed
		 * leading to undefined behaviour. The class
		 * doesn't returns r-value tuple with values,
		 * intead, it moves task (With coroutine_handle's and a latch)
		 * to another class, that acts as tuple.
		 * @tparam Awaitables ... Awaitable types, that all will be co_await'ed
		 */
		template<coro::concepts::awaitable ... Awaitables>
		struct GatherAwaitable {

			bool await_ready() {
				return sizeof...(Awaitables) == 0;
			}

			auto await_suspend(std::coroutine_handle<> awaiting_coroutine) {
				return coro::concepts::get_awaiter(when_all_task).await_suspend(awaiting_coroutine);
			}

			auto await_resume() {
				return GatherTasksStorage<Awaitables ...>(std::move(when_all_task));
			}

			WhenAllAwaitable<Awaitables ...> when_all_task;
		};

		/**
		 * @brief Get GatherTasksStorage reslt for task at index
		 * @tparam Index of task to get value
		 * @param storage R-Value storage with tasks
		 * @returns Moved task value from at 'Index'
		 */
		// tuple_value binds to when_all_task's m_tasks, which lives inside the
		// caller-owned GatherTasksStorage (that is its whole purpose), so the
		// reference returned here outlives get(). MSVC's C4172 is a back-end
		// warning that misreads the std::move chain as returning a reference to a
		// local; it is only silenced by a pragma wrapping the whole function.
#pragma warning(push)
#pragma warning(disable: 4172)
		template<size_t Index, typename ... Awaitables>
		decltype(auto) get(GatherTasksStorage<Awaitables ...>&& storage) {
			auto&& tuple_value = coro::concepts::get_awaiter(std::move(storage.when_all_task)).await_resume();
			return std::move(std::get<Index>(std::move(tuple_value)).return_value());
		}
#pragma warning(pop)
	}

	// just to enable user access outside detail. Cannot be moved here because of ADL
	using detail::get;

	using coro::when_all;

	/**
	 * @brief co_await all awaitables in parallel if possible
	 * @param stop_source stop source, that can stop the awaitables
	 * @param awaitables Awaitables, that will be co_await'ed
	 * @return Awaitable, when co_await'ed returns tuple with tasks.
	 *         Call return_value() to get values or rethrow exception.
	 */
	template<coro::concepts::awaitable ... Awaitables>
	auto when_all(const std::stop_source& stop_source, Awaitables ... awaitables) {
		(detail::try_connect(awaitables, stop_source), ...);

		return coro::when_all(std::move(awaitables) ...);
	}

	/**
	 * @brief co_await all awaitables and return its return values
	 * The function returns only when *all* tasks are finished
	 * @param awaitables Awaitables, that will be co_await'ed
	 * @return Awaitable, when co_await'ed returns tuple-like object
	 *         with all the values from 'awaitables'
	 */
	template<coro::concepts::awaitable ... Awaitables>
	auto gather(Awaitables ... awaitables) {
		return detail::GatherAwaitable<Awaitables ...>{
			coro::when_all(std::move(awaitables) ...)
		};
	}

	/**
	 * @brief co_await all awaitables and return its return values
	 * The function returns only when *all* tasks are finished
	 * @param stop_source stop source, that can stop the awaitables
	 * @param awaitables Awaitables, that will be co_await'ed
	 * @return Awaitable, when co_await'ed returns tuple-like object
	 *         with all the values from 'awaitables'
	 */
	template<coro::concepts::awaitable ... Awaitables>
	auto gather(const std::stop_source& stop_source, Awaitables ... awaitables) {
		(detail::try_connect(awaitables, stop_source), ...);

		return detail::GatherAwaitable<Awaitables ...>{
			coro::when_all(std::move(awaitables) ...)
		};
	}
}

/// Total results count of GatherTasksStorage
template<coro::concepts::awaitable ... Awaitables>
struct std::tuple_size<asyncnet::detail::GatherTasksStorage<Awaitables ...>> :
	std::integral_constant<size_t, sizeof...(Awaitables)> {
};

/// Return type of get(GatherTasksStorage) function
template<size_t Index, coro::concepts::awaitable ... Awaitables>
struct std::tuple_element<Index, asyncnet::detail::GatherTasksStorage<Awaitables ...>> {
	using type = std::remove_reference_t<decltype(std::declval<std::tuple_element_t<Index, asyncnet::detail::WhenAllAwaitableTuple<Awaitables ...>>>().return_value())>;
};