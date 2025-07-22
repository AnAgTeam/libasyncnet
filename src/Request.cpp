#include <asyncnet/Request.hpp>

#include <curlpp/Options.hpp>

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

	Request::Request(std::string_view url) : url_(url) {

	}

	Request::Request(const Request& request_copy, std::string_view url) : handle_(request_copy.clone_handle()), url_(url) {
		handle_.setOpt(curlpp::options::Url(url_));
	}

	void Request::set_max_redirects(const std::optional<long>& max_redirects) {
		handle_.setOpt(cURLpp::Options::FollowLocation(max_redirects.has_value()));
		handle_.setOpt(cURLpp::Options::MaxRedirs(max_redirects.value_or(0)));
	}

	void Request::set_timeout(const std::optional<std::chrono::system_clock::duration>& timeout) {
		using namespace std::chrono;

		const auto timeout_secs = duration_cast<seconds>(timeout.value_or(seconds(0))).count();
		handle_.setOpt(curlpp::options::Timeout(timeout_secs));
	}

	void Request::set_verbose(const bool& is_verbose) {
		handle_.setOpt(curlpp::options::Verbose(is_verbose));
	}

	void Request::set_url_parameters(const UrlParameters& params) {
		std::string params_url = params.apply(url_);
		handle_.setOpt(curlpp::options::Url(params_url));
	}

	void Request::set_headers(const std::list<std::string>& headers) {
		handle_.setOpt(curlpp::options::HttpHeader(headers));
	}

	void Request::add_headers(const std::list<std::string>& headers) {
		curlpp::options::HttpHeader headers_option;
		handle_.getOpt(&headers_option);

		std::list<std::string> new_headers = headers_option.getValue();
		new_headers.insert(new_headers.end(), headers.begin(), headers.end());
		handle_.setOpt(curlpp::options::HttpHeader(std::move(new_headers)));
	}

	void Request::set_cookie_file(const std::string& cookie_file) {
		handle_.setOpt(curlpp::options::CookieFile(cookie_file));
	}

	void Request::set_stop_token(std::stop_token stop_token) {
		auto progress_func = [stop_token = std::move(stop_token)](double, double, double, double) -> int {
			return stop_token.stop_requested() ? curl_cancel_request : curl_continue_request;
		};
		handle_.setOpt(curlpp::options::ProgressFunction(progress_func));
		handle_.setOpt(curlpp::options::NoProgress(false));
	}

	void Request::set_output_stream(std::ostringstream& stream) {
		handle_.setOpt(curlpp::options::WriteStream(&stream));
	}

	curlpp::Easy Request::clone_handle() const {
		return curlpp::Easy(handle_.getCurlHandle().clone());
	}

	PostRequest::PostRequest(std::string_view url, const std::string& data) : Request(url) {
		handle_.setOpt(curlpp::options::PostFields(data));
		handle_.setOpt(curlpp::options::PostFieldSizeLarge(data.length()));
	}


	PostRequest::PostRequest(const Request& copy_request, std::string_view url, const std::string& data) : Request(copy_request, url) {
		handle_.setOpt(curlpp::options::PostFields(data));
		handle_.setOpt(curlpp::options::PostFieldSizeLarge(data.length()));
	}

}
