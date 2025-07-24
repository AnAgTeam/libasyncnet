#include <asyncnet/Request.hpp>

#include <curlpp/Options.hpp>
#include <ranges>

constexpr int curl_cancel_request = 1;
constexpr int curl_continue_request = 0;

namespace asyncnet {

#if defined(_WIN32)
	constexpr std::string_view Request::cookie_memory = "NULL";
#else
	constexpr std::string_view Request::cookie_memory = "/dev/null";
#endif

	Request::Request() {

	}

	Request::Request(std::string_view url) : base_url_(url) {
		set_option<curlpp::options::Url>(base_url_);
	}

	Request::Request(const Request& other, std::string_view url) : Request(other) {
		set_url(url);
	}

	Request::Request(const Request& other) {
		options_.reserve(other.options_.size());
		for (auto& item : other.options_) {
			options_.emplace_back(item->clone());
		}
	}

	curlpp::Easy Request::make_request_handle() const {
		curlpp::Easy handle;
		for (auto& item : options_) {
			handle.setOpt(item->clone());
		}
		return handle;
	}

	void Request::set_url(std::string_view url) {
		base_url_ = url;
		set_option<curlpp::options::Url>(base_url_);
	}

	void Request::set_max_redirects(const std::optional<long>& max_redirects) {
		set_option<cURLpp::Options::FollowLocation>(max_redirects.has_value());
		set_option<cURLpp::Options::MaxRedirs>(max_redirects.value_or(0));
	}

	void Request::set_timeout(const std::optional<std::chrono::system_clock::duration>& timeout) {
		using namespace std::chrono;

		const auto timeout_secs = duration_cast<seconds>(timeout.value_or(seconds(0))).count();
		set_option<curlpp::options::Timeout>(timeout_secs);
	}

	void Request::set_verbose(const bool& is_verbose) {
		set_option<curlpp::options::Verbose>(is_verbose);
	}

	void Request::set_url_parameters(const UrlParameters& params) {
		if (base_url_.empty()) {
			// wtf ?
			return;
		}

		set_url(params.apply(base_url_));
	}

	void Request::set_headers(const std::list<std::string>& headers) {
		set_option<curlpp::options::HttpHeader>(headers);
	}

	void Request::add_headers(const std::list<std::string>& headers) {
		curlpp::options::HttpHeader* headers_option = get_option<curlpp::options::HttpHeader>();
		if (!headers_option) {
			set_option<curlpp::options::HttpHeader>(headers);
			return;
		}
		std::list<std::string> new_headers = headers_option ? headers_option->getValue() : std::list<std::string>();

		new_headers.insert(new_headers.end(), headers.begin(), headers.end());
		headers_option->setValue(new_headers);
	}

	void Request::set_cookie_file(const std::string& cookie_file) {
		set_option<curlpp::options::CookieFile>(cookie_file);
	}

	PostRequest::PostRequest(std::string_view url, const std::string& data) : Request(url) {
		set_option<curlpp::options::PostFields>(data);
		set_option<curlpp::options::PostFieldSizeLarge>(data.length());
	}


	PostRequest::PostRequest(const Request& copy_request, std::string_view url, const std::string& data) : Request(copy_request, url) {
		set_option<curlpp::options::PostFields>(data);
		set_option<curlpp::options::PostFieldSizeLarge>(data.length());
	}

}
