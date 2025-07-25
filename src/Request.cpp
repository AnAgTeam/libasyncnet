#include <asyncnet/Request.hpp>

#include <curlpp/Options.hpp>
#include <ranges>

namespace asyncnet {

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

	void Request::set_cookie_file(std::string_view cookie_file) {
		std::string cookie_file_str(cookie_file);
		set_cookie_file(cookie_file_str);
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

	HeadRequest::HeadRequest(std::string_view url) : Request(url) {
		set_option<curlpp::options::NoBody>(true);
	}

	HeadRequest::HeadRequest(const Request& copy_request, std::string_view url) : Request(copy_request, url) {
		set_option<curlpp::options::NoBody>(true);
	}

	PostMultipartRequest::PostMultipartRequest(std::string_view url) : Request(url) {
		set_forms({});
	}

	PostMultipartRequest::PostMultipartRequest(std::string_view url, const MultipartForms& forms) : Request(url) {
		set_forms(forms);
	}

	
	PostMultipartRequest::PostMultipartRequest(const Request& copy_request, std::string_view url, const MultipartForms& forms) : Request(copy_request, url) {
		set_forms(forms);
	}

	void PostMultipartRequest::set_forms(const MultipartForms& forms) {
		set_option<curlpp::options::HttpPost>(forms);
	}

	void PostMultipartRequest::add_form(const MultipartPart& part) {
		if (auto current_option = get_option<curlpp::options::HttpPost>()) {
			MultipartForms new_forms = current_option->getValue();
			new_forms.push_back(part);
			current_option->setValue(new_forms);
		}
		else {
			set_forms({ part });
		}
	}
}
