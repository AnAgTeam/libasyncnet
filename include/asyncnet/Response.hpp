#pragma once
#include <curlpp/Easy.hpp>
#include <sstream>
#include <string>
#include <optional>

namespace asyncnet {
	class Response {
	public:
		explicit Response(curlpp::Easy&& handle, std::string header_block = {});
		explicit Response(curlpp::Easy&& handle, std::ostringstream&& text_stream, std::string header_block = {});

		Response(const Response& other) = delete;
		Response(Response&& other) = default;

		/**
		 * Get HTTP status code
		 * @return HTTP status code
		 */
		long get_status_code() const;

		/**
		 * Copy response body as string
		 * @return Response body
		 */
		std::string get_text() const &;

		/**
		 * Move response body as string
		 * @return Response body
		 */
		std::string get_text() &&;

		/**
		 * Raw header block of the final response: CRLF-delimited "Name: Value"
		 * lines as received. On a redirect chain only the last response's headers
		 * are kept (intermediate responses' headers are dropped), and the status
		 * line ("HTTP/...") is not included. Empty if the server sent no headers.
		 * @return Response header block
		 */
		const std::string& get_header_block() const &;

		/**
		 * Move the raw header block of the final response.
		 * @return Response header block
		 */
		std::string get_header_block() &&;

		/**
		 * Effective URL after following any redirects (CURLINFO_EFFECTIVE_URL).
		 * @return Final URL the response came from
		 */
		std::string get_effective_url() const;

	private:
		curlpp::Easy handle_;
		std::optional<std::ostringstream> stream_;
		std::string header_block_;
	};
}