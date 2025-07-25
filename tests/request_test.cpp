#include "catch_amalgamated.hpp"

#include <asyncnet/Requestor.hpp>

#include <curlpp/Options.hpp>
#include <coro/sync_wait.hpp>
#include <coro/when_all.hpp>
#include <print>

#include <iostream>

#pragma execution_character_set("utf-8")

using namespace asyncnet;

TEST_CASE("Request options set") {
	Request request("123");

	{
		curlpp::options::Url url_option;
		request.make_request_handle().getOpt(url_option);
		REQUIRE(url_option.getValue() == "123");
	}

	request.set_cookie_file(Request::cookie_memory);
	request.set_headers({
		"User-Agent: YYX"
	});
	request.set_max_redirects(Request::infinite_redirects);
	request.set_timeout(std::nullopt);
	request.set_url("444");
	request.set_verbose(true);
	request.add_headers({
		"Content-Type: application/octet-stream"
	});
	request.set_url_parameters({
		{ "str", "he" }
	});
	
	{
		curlpp::Easy handle = request.make_request_handle();
		curlpp::options::Url url_option;
		curlpp::options::HttpHeader header_option;
		curlpp::options::Verbose verbose_option;
		curlpp::options::Timeout timeout_option;
		curlpp::options::FollowLocation follow_location_option;
		curlpp::options::MaxRedirs max_redirs_option;

		handle.getOpt(url_option);
		handle.getOpt(header_option);
		handle.getOpt(verbose_option);
		handle.getOpt(timeout_option);
		handle.getOpt(follow_location_option);
		handle.getOpt(max_redirs_option);

		std::list<std::string> expected_headers = {
			"User-Agent: YYX",
			"Content-Type: application/octet-stream"
		};

		REQUIRE(url_option.getValue() == "444?str=he");
		REQUIRE(header_option.getValue() == expected_headers);
		REQUIRE(verbose_option.getValue() == true);
		REQUIRE(timeout_option.getValue() == 0);
		REQUIRE(follow_location_option.getValue() == true);
		REQUIRE(max_redirs_option.getValue() == -1);
	}
}