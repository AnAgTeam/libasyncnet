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
	public:
#if defined(_WIN32)
		/// static field passed to @ref set_cookie_file for saving cookies in memory
		static constexpr std::string_view cookie_memory = "NULL";
#else
		/// static field passed to @ref set_cookie_file for saving cookies in memory
		static constexpr std::string_view cookie_memory = "/dev/null";
#endif
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

		Request(const Request& request);
		Request(Request&& request) = default;

		/**
		 * Constructs @ref curlpp::Easy handle to perform request with all options inherited from this Request.
		 * Pass handle to @ref Requstor::perform_handle, to actually make network request. Also, you can pass Request directly to @ref Requestor::perform_request
		 * @return Request handle
		 */
		curlpp::Easy make_request_handle() const;

		/**
		 * Set request new url. Note that it will clear all UrlParameters setted before!
		 * @param url The url to set
		 */
		void set_url(std::string_view url);

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
		void set_cookie_file(std::string_view cookie_file);


	protected:

		/**
		 * Set Request option. If Request has already contains this option, it would set containing value. Otherwise it creates new option
		 * @tparam Option Option type to be setted
		 * @tparam OptionArg Option construct arguments
		 */
		template<typename Option, typename OptionArg>
		void set_option(OptionArg&& arg) {
			if (Option* vec_option = get_option<Option>()) {
				vec_option->setValue(std::forward<OptionArg>(arg));
			}
			else {
				options_.push_back(std::make_unique<Option>(std::forward<OptionArg>(arg)));
			}
		}

		/**
		 * Get request option.
		 * @tparam Option type to be getted
		 * @return If Request contains Option, return pointer to it. Otherwise, return nullptr
		 */
		template<typename Option>
		Option* get_option() {
			auto iter = std::find_if(options_.begin(), options_.end(), [](const std::unique_ptr<curlpp::OptionBase>& vec_option) {
				return Option::option == vec_option->getOption();
			});

			return iter != options_.end() ? static_cast<Option*>(iter->get()) : nullptr;
		}

		std::vector<std::unique_ptr<curlpp::OptionBase>> options_;
		std::string base_url_;
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

	class HeadRequest : public Request {
	public:
		/** @copydoc Request::Request(url)
		 * Constructs HEAD request
		 * @param url Request URL
		 */
		HeadRequest(std::string_view url);

		/** @copydoc Request::Request(url)
		 * Constructs HEAD request
		 * @param url Request URL
		 */
		HeadRequest(const Request& copy_request, std::string_view url);
	};

	class PostMultipartRequest : public Request {
	public:
		/** @copydoc Request::Request(url)
		 * Constructs multipart POST request
		 * @param url Request URL
		 */
		PostMultipartRequest(std::string_view url);

		/** @copydoc Request::Request(url)
		 * Constructs multipart POST request with given POST forms
		 * @param url Request URL
		 * @param forms Request forms
		 */
		PostMultipartRequest(std::string_view url, const MultipartForms& forms);

		/** @copydoc Request::Request(url)
		 * Constructs multipart POST request with given POST forms
		 * @param url Request URL
		 * @param forms Request forms
		 */
		PostMultipartRequest(const Request& copy_request, std::string_view url, const MultipartForms& forms);

		/**
		 * Set multipart POST forms
		 * @param forms The forms to set
		 */
		void set_forms(const MultipartForms& forms);

		/**
		 * Add new multipart POST form to existing ones
		 * @param part The part to add
		 */
		void add_form(const MultipartPart& part);
	};
};