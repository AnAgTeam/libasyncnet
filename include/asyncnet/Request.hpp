#pragma once
#include <asyncnet/utility/Concepts.hpp>
#include <asyncnet/NetTypes.hpp>
#include <asyncnet/CurlShared.hpp>

#include <curlpp/Easy.hpp>
#include <list>
#include <stop_token>
#include <optional>
#include <sstream>
#include <chrono>

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
		 * Constructs invalid request without URL
		 */
		Request();

		/**
		 * Constructs with URL. By default it's GET request
		 * @param url URL to server
		 */
		explicit Request(std::string url);

		/**
		 * Copies options from copy_request, and then sets the URL
		 * @param copy_request The request to copy options from
		 * @param url URL to server
		 */
		explicit Request(const Request& copy_request, std::string url);

		Request(const Request& request);
		Request(Request&& request) = default;

		/**
		 * Constructs @ref curlpp::Easy handle to perform request with all options inherited from this Request.
		 * Pass handle to @ref Requstor::perform_handle, to actually make network request.
		 * Also, you can pass Request directly to @ref Requestor::perform_request
		 * @return Request handle
		 */
		curlpp::Easy make_request_handle() const;

		/**
		 * Inherit all options and the share context from another request, keeping
		 * this request's own URL. Existing options on this request are replaced by
		 * the inherited ones; the URL set on this request (if any) is preserved.
		 * @param other The request to inherit options from
		 * @return Reference to this Request
		 */
		Request& inherit_from(const Request& other);

		/**
		 * Set request new url. Note that it will clear all UrlParameters setted before!
		 * @param url The url to set
		 */
		void set_url(std::string url);

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
		 * By default cookies don't saved.
		 * @param cookie_file Cookies file path
		 */
		void set_cookie_file(std::string_view cookie_file);

		/**
		 * Set the request share context. It is used to share cookies, ssl, connection data, etc.
		 * @see CurlShared
		 * @param share The share context
		 */
		void set_share(std::shared_ptr<CurlShared> share) noexcept;

		/**
		 * Get the request share context.
		 * @see CurlShared
		 * @param share The share context
		 */
		const std::shared_ptr<CurlShared>& get_share() const noexcept;

		/**
		 * Stream the response body into a caller-owned stream instead of buffering
		 * it internally. Enables downloading large bodies straight to a file (or any
		 * @ref std::ostream) as the data arrives, without holding the whole body in memory.
		 * @note The stream must stay alive until the request's task completes.
		 *       When a sink is set, @ref Response::get_text() returns an empty string,
		 *       because the body was written to the sink rather than buffered.
		 *       By default no sink is set and the body is buffered internally.
		 * @param stream The output stream to receive the body, or nullptr to buffer internally
		 */
		void set_output_stream(std::ostream* stream) noexcept;

		/**
		 * Get the caller-owned output stream the response body will be written to.
		 * @return The output stream, or nullptr if the body is buffered internally (the default)
		 */
		[[nodiscard]] std::ostream* get_output_stream() const noexcept;

		/**
		 * Set Request any curl option, except url. To set url please use @ref set_url(url) and @ref set_url_parameters(params).
		 * If Request has already contains this option, it would set containing value. Otherwise it creates new option
		 * @tparam Option Option type to be setted
		 * @tparam OptionArg Option construct arguments
		 */
		template<typename Option, typename OptionArg>
			requires (Option::option != CURLOPT_URL)
		void set_option(OptionArg&& arg) {
			set_option_<Option>(std::forward<OptionArg>(arg));
		}

		/**
		 * Get request option.
		 * @tparam Option type to be getted
		 * @return If Request contains Option, return raw pointer to it. Otherwise, return nullptr
		 */
		template<typename Option>
		auto get_option() {
			return get_option_<Option>();
		}

	protected:

		/**
		 * Set Request option. If Request has already contains this option, it would set containing value. Otherwise it creates new option
		 * @tparam Option Option type to be setted
		 * @tparam OptionArg Option construct arguments
		 */
		template<typename Option, typename OptionArg>
		void set_option_(OptionArg&& arg) {
			if (Option* vec_option = get_option_<Option>()) {
				vec_option->setValue(std::forward<OptionArg>(arg));
			}
			else {
				options_.push_back(std::make_unique<Option>(std::forward<OptionArg>(arg)));
			}
		}

		/**
		 * Get request option.
		 * @tparam Option type to be getted
		 * @return If Request contains Option, return raw pointer to it. Otherwise, return nullptr
		 */
		template<typename Option>
		Option* get_option_() {
			auto iter = std::find_if(options_.begin(), options_.end(), [](const std::unique_ptr<curlpp::OptionBase>& vec_option) {
				return Option::option == vec_option->getOption();
			});

			return iter != options_.end() ? static_cast<Option*>(iter->get()) : nullptr;
		}

		std::vector<std::unique_ptr<curlpp::OptionBase>> options_;
		std::string base_url_;
		std::shared_ptr<CurlShared> share_;
		std::ostream* output_stream_ = nullptr;
	};

	class PostRequest : public Request {
	public:
		/** @copydoc Request::Request(url)
		 * Constructs POST request with given data
		 * @param url Request URL
		 * @param data Request POST data
		 */
		explicit PostRequest(std::string url, const std::string& data);

		/** @copydoc Request::Request(copy_request, url)
		 * Constructs POST request with given data
		 * @param copy_request Request to copy options from
		 * @param url Request URL
		 * @param data Request POST data
		 */
		explicit PostRequest(const Request& copy_request, std::string url, const std::string& data);

	private:

	};

	using GetRequest = Request;

	class HeadRequest : public Request {
	public:
		/** @copydoc Request::Request(url)
		 * Constructs HEAD request
		 * @param url Request URL
		 */
		HeadRequest(std::string url);

		/** @copydoc Request::Request(url)
		 * Constructs HEAD request
		 * @param url Request URL
		 */
		HeadRequest(const Request& copy_request, std::string url);
	};

	class PostMultipartRequest : public Request {
	public:
		/** @copydoc Request::Request(url)
		 * Constructs multipart POST request
		 * @param url Request URL
		 */
		PostMultipartRequest(std::string url);

		/** @copydoc Request::Request(url)
		 * Constructs multipart POST request with given POST forms
		 * @param url Request URL
		 * @param forms Request forms
		 */
		PostMultipartRequest(std::string url, const MultipartForms& forms);

		/** @copydoc Request::Request(url)
		 * Constructs multipart POST request with given POST forms
		 * @param url Request URL
		 * @param forms Request forms
		 */
		PostMultipartRequest(const Request& copy_request, std::string url, const MultipartForms& forms);

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