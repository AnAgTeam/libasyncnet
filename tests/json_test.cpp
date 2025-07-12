#include "catch_amalgamated.hpp"
#include <asyncnet/JsonConversions.hpp>

#pragma execution_character_set("utf-8")

namespace boost::json {
	using asyncnet::json::timestamp::tag_invoke;
};

TEST_CASE("asyncnet::json to/from time_point") {
	using UnsignedTimePoint = std::chrono::time_point<std::chrono::system_clock, std::chrono::duration<unsigned long long>>;
	using TimePoint = std::chrono::time_point<std::chrono::system_clock, std::chrono::seconds>;
	using TimePointMinutes = std::chrono::time_point<std::chrono::system_clock, std::chrono::minutes>;

	boost::json::value json_value = 13;
	boost::json::value json_value_unsigned = 14U;
	boost::json::value json_value_null;
	
	REQUIRE(boost::json::value_to<TimePoint>(json_value) == TimePoint(std::chrono::seconds(13)));
	REQUIRE(boost::json::value_to<UnsignedTimePoint>(json_value_unsigned) == TimePoint(std::chrono::seconds(14)));

	REQUIRE_THROWS_AS(boost::json::value_to<UnsignedTimePoint>(json_value), boost::system::system_error);
	REQUIRE_THROWS_AS(boost::json::value_to<TimePoint>(json_value_null), boost::system::system_error);

	REQUIRE(boost::json::value_from(TimePoint(std::chrono::seconds(13))) == json_value);
	REQUIRE(boost::json::value_from(UnsignedTimePoint(std::chrono::seconds(13))) == json_value);

	REQUIRE(boost::json::value_from(TimePointMinutes(std::chrono::minutes(13))) == json_value);
}


TEST_CASE("asyncnet::json to/from duration") {
	using UnsignedDuration = std::chrono::duration<unsigned long long>;

	boost::json::value json_value = 13;
	boost::json::value json_value_120s = 120;
	boost::json::value json_value_unsigned = 14U;
	boost::json::value json_value_null;

	REQUIRE(boost::json::value_to<std::chrono::seconds>(json_value) == std::chrono::seconds(13));
	REQUIRE(boost::json::value_to<std::chrono::seconds>(json_value_120s) == std::chrono::minutes(2));
	REQUIRE(boost::json::value_to<UnsignedDuration>(json_value_unsigned) == UnsignedDuration(14));

	REQUIRE_THROWS_AS(boost::json::value_to<UnsignedDuration>(json_value), boost::system::system_error);
	REQUIRE_THROWS_AS(boost::json::value_to<std::chrono::seconds>(json_value_null), boost::system::system_error);

	REQUIRE(boost::json::value_from(std::chrono::seconds(13)) == json_value);
}