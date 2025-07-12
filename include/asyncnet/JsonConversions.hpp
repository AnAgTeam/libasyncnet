#pragma once
#include <concepts>
#include <chrono>
#include <boost/json.hpp>

namespace asyncnet::json::timestamp {

	using boost::json::value_from_tag;
	using boost::json::value_to_tag;
	using boost::json::value;

	/**
	 * @ref boost::json::tag_invoke for converting to @ref std::chrono::time_point with respect to clock duration. If duration is unsigned, also check value is unsigned
	 * @return Returns converted @ref std::chrono::time_point
	 */
	template<typename Clock, typename Duration>
	auto tag_invoke(const value_to_tag<std::chrono::time_point<Clock, Duration>>&, const value& value) {
		const bool should_as_unsigned = value.is_uint64() || std::unsigned_integral<typename Duration::rep>;
		const auto integer_ts = should_as_unsigned ? value.as_uint64() : value.as_int64();
		return std::chrono::time_point<Clock, Duration>(Duration(integer_ts));
	}

	/**
	 * @ref boost::json::tag_invoke for converting from @ref std::chrono::time_point with respect to clock duration
	 * @return Returns converted @ref std::chrono::time_point
	 */
	template<typename Clock, typename Duration>
	void tag_invoke(const value_from_tag&, value& out_value, const std::chrono::time_point<Clock, Duration>& time) {
		const auto time_duration = time.time_since_epoch();
		out_value = time_duration.count();
	}

	/**
	 * @ref boost::json::tag_invoke for converting to @ref std::chrono::duration. If duration is unsigned, also check value is unsigned
	 * @return Returns converted @ref std::chrono::duration
	 */
	template<typename Rep, typename Period>
	auto tag_invoke(const value_to_tag<std::chrono::duration<Rep, Period>>&, const value& value) {
		const bool should_as_unsigned = value.is_uint64() || std::unsigned_integral<Rep>;
		const auto integer_ts = should_as_unsigned ? value.as_uint64() : value.as_int64();
		return std::chrono::duration<Rep, Period>(Rep(integer_ts));
	}

	/**
	 * @ref boost::json::tag_invoke for converting from @ref std::chrono::duration
	 * @return Returns converted @ref std::chrono::duration
	 */
	template<typename Rep, typename Period>
	void tag_invoke(const value_from_tag&, value& out_value, const std::chrono::duration<Rep, Period>& duration) {
		out_value = duration.count();
	}
}