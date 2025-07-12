#include <asyncnet/UrlSession.hpp>
#include <asyncnet/detail/Format.hpp>

#include <sstream>
#include <curlpp/Options.hpp>
#include <curlpp/Infos.hpp>

constexpr int curl_cancel_request = 1;
constexpr int curl_continue_request = CURL_PROGRESSFUNC_CONTINUE;

namespace asyncnet {
#if defined(_WIN32)
	constexpr std::string_view AsyncNetworkSession::cookie_memory = "NULL";
#else
	constexpr std::string_view AsyncNetworkSession::cookie_memory = "/dev/null";
#endif

	void AsyncNetworkSession::init() {
		curlpp::initialize();
	}

	void AsyncNetworkSession::deinit() {
		curlpp::terminate();
	}

	AsyncNetworkSession::AsyncNetworkSession(unsigned worker_count) : NetworkRequestor(worker_count) {
		initialize_handle();
	}

	AsyncNetworkSession::AsyncNetworkSession(NetworkRequestor&& requestor) : NetworkRequestor(std::move(requestor)) {
		initialize_handle();
	}

	void AsyncNetworkSession::initialize_handle() {
		set_verbose(false);
		set_timeout(10);
		set_cookie_file(std::string(cookie_memory));

		base_handle_.setOpt(curlpp::options::SslVerifyPeer(true));
		base_handle_.setOpt(curlpp::options::HttpHeader(default_headers_));
	}

	void AsyncNetworkSession::enable_redirects(const bool value, const long max) {
		base_handle_.setOpt(cURLpp::Options::FollowLocation(value));
		if (value) {
			base_handle_.setOpt(cURLpp::Options::MaxRedirs(max));
		}

	}

	void AsyncNetworkSession::set_verbose(const bool value) {
		base_handle_.setOpt(curlpp::options::Verbose(value));
	}

	void AsyncNetworkSession::set_timeout(const long value) {
		base_handle_.setOpt(curlpp::options::Timeout(value));
	}

	void AsyncNetworkSession::set_default_headers(const std::list<std::string>& headers) {
		default_headers_ = headers;
		base_handle_.setOpt(curlpp::options::HttpHeader(default_headers_));
	}

	void AsyncNetworkSession::add_default_header(const std::string& header) {
		default_headers_.push_back(header);
	}

	void AsyncNetworkSession::set_cookie_file(const std::string& filename) {
		base_handle_.setOpt(curlpp::options::CookieFile(filename));
	}

	coro::task<std::string> AsyncNetworkSession::get_request(const std::string url, const std::vector<std::string> extra_headers, const UrlParameters params, std::optional<std::stop_token> stop_token) const {
		curlpp::Easy op_handle(base_handle_.getCurlHandle().clone());
		std::ostringstream stream;

		op_handle.setOpt(curlpp::options::WriteStream(&stream));
		if (!params.empty()) {
			op_handle.setOpt(curlpp::options::Url(params.apply(url)));
		}
		else {
			op_handle.setOpt(curlpp::options::Url(url));
		}

		if (stop_token) {
			auto progress_func = [stop_token = std::move(*stop_token)](double, double, double, double) -> int {
				std::cout << "Progress" << std::endl;
				return stop_token.stop_requested() ? curl_cancel_request : curl_continue_request;
			};
			op_handle.setOpt(curlpp::options::ProgressFunction(progress_func));
			op_handle.setOpt(curlpp::options::NoProgress(false));
		}

		if (!extra_headers.empty()) {
			op_handle.setOpt(curlpp::options::HttpHeader(make_expanded_header(extra_headers)));
		}

		co_await perform_handle(op_handle);
		co_return stream.str();
	}

	coro::task<std::string> AsyncNetworkSession::post_request(const std::string url, const std::string data, std::string_view content_type, const std::vector<std::string> extra_headers, const UrlParameters params, std::optional<std::stop_token> stop_token) const {
		curlpp::Easy op_handle(base_handle_.getCurlHandle().clone());
		std::ostringstream stream;

		op_handle.setOpt(curlpp::options::WriteStream(&stream));
		if (!params.empty()) {
			op_handle.setOpt(curlpp::options::Url(params.apply(url)));
		}
		else {
			op_handle.setOpt(curlpp::options::Url(url));
		}
		op_handle.setOpt(curlpp::options::PostFields(data));
		op_handle.setOpt(curlpp::options::PostFieldSizeLarge(data.length()));

		std::list<std::string> new_headers = make_expanded_header(extra_headers);
		if (!content_type.empty()) {
			new_headers.push_back(detail::format("Content-Type: {}; Charset=utf-8", content_type));
		}
		op_handle.setOpt(curlpp::options::HttpHeader(new_headers));

		co_await perform_handle(op_handle);
		co_return stream.str();
	}

	coro::task<std::string> AsyncNetworkSession::post_multipart_request(const std::string url, const MultipartForms forms, const std::vector<std::string> extra_headers, const UrlParameters params) const {
		curlpp::Easy op_handle(base_handle_.getCurlHandle().clone());
		std::ostringstream stream;

		op_handle.setOpt(curlpp::options::WriteStream(&stream));
		if (!params.empty()) {
			op_handle.setOpt(curlpp::options::Url(params.apply(url)));
		}
		else {
			op_handle.setOpt(curlpp::options::Url(url));
		}
		op_handle.setOpt(curlpp::options::HttpPost(forms));

		if (!extra_headers.empty()) {
			op_handle.setOpt(curlpp::options::HttpHeader(make_expanded_header(extra_headers)));
		}

		co_await perform_handle(op_handle);
		co_return stream.str();
	}

	std::list<std::string> AsyncNetworkSession::make_expanded_header(const std::vector<std::string>& extra_headers) const {
		std::list<std::string> new_headers = default_headers_;
		for (auto& header : extra_headers) {
			new_headers.push_back(header);
		}
		return new_headers;
	}

};
