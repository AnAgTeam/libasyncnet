#pragma once
#include <curlpp/Easy.hpp>
#include <sstream>
#include <optional>

namespace asyncnet {
	class Response {
	public:
		explicit Response(curlpp::Easy handle);
		explicit Response(curlpp::Easy handle, std::ostringstream&& text_stream);

		Response(const Response& other) = delete;
		Response(Response&& other) = default;

		/**
		 * Get HTTP status code
		 * @return HTTP status code
		 */
		long get_status_code() const;

		/**
		 * Get response body as string. If response not passed, return empty string
		 * @return Response body
		 */
		std::string get_text() const;

	private:
		curlpp::Easy handle_;
		std::optional<std::ostringstream> stream_;
	};
}