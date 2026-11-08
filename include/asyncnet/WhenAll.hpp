#pragma once
#include <asyncnet/Gather.hpp>
#include <coro/when_all.hpp>

namespace asyncnet {
	namespace detail {
		template<coro::concepts::awaitable T>
		void try_connect(T&, const std::stop_source&) {

		}

		template<coro::concepts::awaitable T>
			requires concepts::derived_from_template<T, CancellingTask>
		void try_connect(T& awaitable, const std::stop_source& src) {
			awaitable.update_stop_source(src);
		}

	}

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

}
