#include "catch_amalgamated.hpp"
#include <asyncnet/NetTypes.hpp>

#pragma execution_character_set("utf-8")

using namespace asyncnet;

TEST_CASE("UrlEncoded escape") {
	REQUIRE(url_escape(R"(fa()!@#:<>?&!$@!";%'§рт:11)") == "fa%28%29%21%40%23%3A%3C%3E%3F%26%21%24%40%21%22%3B%25%27%D1%8D%D0%B0%D0%B2%3A11");
}

TEST_CASE("UrlParameter construction") {
	UrlParameters params;
	REQUIRE(params.empty());
	REQUIRE(params.get() == "");
}

TEST_CASE("UrlParameters initializer list constuction") {
	UrlParameters params = {
		{ "test_int", "1" },
		{ "test_str", "test" }
	};

	std::string url = "https://google.com";

	REQUIRE(params.apply(url) == "https://google.com?test_int=1&test_str=test");
	REQUIRE(params.get() == "test_int=1&test_str=test");
	REQUIRE_FALSE(params.empty());
}

TEST_CASE("UrlParameter expand copy") {
	UrlParameters params = {
		{ "test_int", "1" },
		{ "test_str", "test" }
	};

	UrlParameters expanded_params = params.expand_copy({
		{ "test_bool", "true" }
	});

	REQUIRE(expanded_params.get() == "test_int=1&test_str=test&test_bool=true");
}

TEST_CASE("UrlParameter operator=") {
	UrlParameters params;
	params = {
		{ "test_int", "1" },
		{ "test_str", "test" }
	};

	REQUIRE(params.get() == "test_int=1&test_str=test");
}