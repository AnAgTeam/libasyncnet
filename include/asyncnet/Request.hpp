#pragma once
#include <asyncnet/detail/Concepts.hpp>
#include <asyncnet/NetTypes.hpp>

#include <curlpp/Easy.hpp>
#include <list>
#include <stop_token>
#include <optional>
#include <sstream>

namespace asyncnet {

	class Request {
		friend class Requestor;
		friend class AsyncSession;

	public:
		/// static field passed to @ref set_cookie_file for saving cookies in memory
		static const std::string_view cookie_memory;
		/// static field passed to @ref set_max_redirects for infinite redirects count
		static constexpr long infinite_redirects = -1;

		/**
		 * Constructs invalid request without URL. Used only as options container
		 */
		Request();

		/**
		 * Constructs with URL. By default it's GET request
		 * @param url URL to server
		 */
		explicit Request(std::string_view url);

		/**
		 * Copies options from copy_request, and then sets the URL
		 * @param copy_request The request to copy options from
		 * @param url URL to server
		 */
		explicit Request(const Request& copy_request, std::string_view url);

		Request(const Request& request) = default;
		Request(Request&& request) = default;

		/**
		 * Creates @ref std::shared_ptr from Request
		 * @param args Arguments to pass to the Request constructor
		 */
		template<std::constructible_from<Request> ... Args>
		std::shared_ptr<Request> make_shared(Args&& ... args) {
			return std::make_shared<Request>(std::forward<Args>(args) ...);
		}

		/**
		 * Set maximum redirects count for the request. If passed @ref std::nullopt, it means no redirects.
		 * Can be passed @ref infinite_redirects for infinite redirects count (allows infinite redirect loop).
		 * By default setted to @ref std::nullopt
		 * @param max_redirects Maximum redirects count or @ref std::nullopt
		 */
		void set_max_redirects(const std::optional<long>& max_redirects);

		/**
		 * Set timeout for the request, the request will raise exception if timeout exceeds.
		 * If passed @ref std::nullopt, it means no timeout, the request can wait infinitely long.
		 * By default setted to @ref std::nullopt
		 * @param timeout Timeout or @ref std::nullopt
		 */
		void set_timeout(const std::optional<std::chrono::system_clock::duration>& timeout);

		/**
		 * Set the reques verbosity. If setted to true, debug information will be printed to stdout.
		 * By default setted to false
		 * @param is_verbose Set to true for verbosity
		 */
		void set_verbose(const bool& is_verbose);

		/**
		 * Set URL parameters for the request. By default no parameters is passed
		 * @param params URL parameters for the request
		 */
		void set_url_parameters(const UrlParameters& params);

		/**
		 * Set URL headers for the request. By default no headers is passed
		 * @param headers URL headers for the request
		 */
		void set_headers(const std::list<std::string>& headers);

		/**
		 * Adds new headers to existing.
		 * @param headers URL headers for the request
		 */
		void add_headers(const std::list<std::string>& headers);

		/**
		 * Set the file in which the cookies will be stored. Can be passed @ref cookie_memory to save cookies in memory.
		 * By default cookies don't saved
		 * @param cookie_file Cookies file path
		 */
		void set_cookie_file(const std::string& cookie_file);

		/**
		 * Set the file in which the cookies will be stored. Can be passed @ref cookie_memory to save cookies in memory.
		 * By default cookies don't saved
		 * @param cookie_file Cookies file path
		 */
		void set_stop_token(std::stop_token stop_token);

	protected:

		void set_output_stream(std::ostringstream& stream);
		curlpp::Easy clone_handle() const;

		curlpp::Easy handle_;

		std::string url_;
	};

	class PostRequest : public Request {
	public:
		/** @copydoc Request::Request(url)
		 * Constructs POST request with given data
		 * @param url Request URL
		 * @param data Request POST data
		 */
		explicit PostRequest(std::string_view url, const std::string& data);

		/** @copydoc Request::Request(copy_request, url)
		 * Constructs POST request with given data
		 * @param copy_request Request to copy options from
		 * @param url Request URL
		 * @param data Request POST data
		 */
		explicit PostRequest(const Request& copy_request, std::string_view url, const std::string& data);

	private:

	};

	using GetRequest = Request;
};