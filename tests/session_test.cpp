#include "catch_amalgamated.hpp"

#include <asyncnet/AsyncSession.hpp>
#include <asyncnet/CancellingTask.hpp>

#include <coro/sync_wait.hpp>
#include <print>

#pragma execution_character_set("utf-8")

using namespace asyncnet;

TEST_CASE("AsyncSession cancellation") {
	AsyncSession session(1);

	auto worker = [](AsyncSession& session) -> coro::task<void> {
		auto request = session.make_request<GetRequest>("https://google.com");

		try {
			auto task = session.perform_request(request);
			// should immediately stop
			task.request_stop();

			co_await task;

			// never reach here
			REQUIRE(false);
		}
		catch (const NetworkRuntimeError& e) {
			if (e.whatCode() != CancelledErrorCode) {
				std::rethrow_exception(std::current_exception());
			}
		}
	};

	coro::sync_wait(worker(session));
}

TEST_CASE("AsyncSession cancellation from other coroutine") {
	AsyncSession session(1);

	auto worker = [](AsyncSession& session) -> coro::task<void> {
		auto net_worker = [](AsyncSession& session) -> CancellingTask<void> {
			try {
				auto request = session.make_request<GetRequest>("https://google.com");
				auto task = session.perform_request(request).update_stop_source(co_await awaitables::get_stop_source);

				co_await task;

				// never reach here
				REQUIRE(false);
			}
			catch (const NetworkRuntimeError& e) {
				if (e.whatCode() != CancelledErrorCode) {
					std::rethrow_exception(std::current_exception());
				}
			}
		};
		
		auto task = net_worker(session);
		task.request_stop();
		co_await task;

	};

	coro::sync_wait(worker(session));
}

#ifdef ASYNCNET_ENABLE_TESTS_NETWORK

TEST_CASE("AsyncSession GET request") {
	AsyncSession session(1);

	auto worker = [](AsyncSession& session) -> coro::task<void> {
		boost::json::object expected_args = {
			{ "test", "test1" }
		};

		auto request = session.make_request<GetRequest>("https://httpbin.org/get");
		request.set_url_parameters({
			{ "test", "test1" }
		});
		request.add_headers({
			"Test-Header: 123"
		});

		auto resp = co_await session.perform_request(request);
		CHECK(resp.get_status_code() == 200);

		boost::json::object resp_object = parse_json_object(resp.get_text());
		INFO(resp_object);

		boost::json::object resp_headers = resp_object["headers"].as_object();
		REQUIRE(resp_headers.find("Test-Header") != resp_headers.end());
		REQUIRE(resp_headers.find("Test-Header")->value().as_string() == "123");

		REQUIRE(resp_object["args"].as_object() == expected_args);
	};

	coro::sync_wait(worker(session));
}

TEST_CASE("AsyncSession POST request") {
	AsyncSession session(1);

	auto worker = [](AsyncSession& session) -> coro::task<void> {
		std::string test_data = "testdata=true&str=mmm";
		boost::json::object expected_json_form = {
			{ "str", "mmm" },
			{ "testdata", "true" }
		};

		auto request = session.make_request<PostRequest>("https://httpbin.org/post", test_data);

		auto resp = co_await session.perform_request(request);
		CHECK(resp.get_status_code() == 200);

		boost::json::object resp_object = parse_json_object(resp.get_text());
		INFO(resp_object);
		REQUIRE(resp_object["form"].as_object() == expected_json_form);
	};

	coro::sync_wait(worker(session));
}

TEST_CASE("AsyncSession HEAD request") {
	AsyncSession session(1);

	auto worker = [](AsyncSession& session) -> coro::task<void> {
		auto request = session.make_request<HeadRequest>("https://httpbin.org/get");

		auto resp = co_await session.perform_request(request);
		CHECK(resp.get_status_code() == 200);

		REQUIRE(resp.get_status_code() == 200);
		REQUIRE(resp.get_text().empty());
	};

	coro::sync_wait(worker(session));
}

TEST_CASE("AsyncSession multipart POST request") {
	AsyncSession session(1);

	auto worker = [](AsyncSession& session) -> coro::task<void> {
		MultipartPart test_data_content = new MultipartContentPart("test", "test1");
		MultipartForms test_data_forms = { test_data_content };
		boost::json::object expected_json_form = {
			{ "test", "test1" }
		};

		auto request = session.make_request<PostMultipartRequest>("https://httpbin.org/post", test_data_forms);

		auto resp = co_await session.perform_request(request);
		CHECK(resp.get_status_code() == 200);

		boost::json::object resp_object = parse_json_object(resp.get_text());
		INFO(resp_object);

		REQUIRE(resp_object["form"].as_object() == expected_json_form);

		boost::json::object resp_headers = resp_object["headers"].as_object();
		REQUIRE(resp_headers.find("Content-Type") != resp_headers.end());
		REQUIRE(resp_headers.find("Content-Type")->value().as_string().starts_with("multipart/form-data;"));
	};

	coro::sync_wait(worker(session));
}

#endif