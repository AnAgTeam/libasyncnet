#include <asyncnet/AsyncSession.hpp>
#include <asyncnet/detail/Format.hpp>

#include <sstream>
#include <curlpp/Options.hpp>
#include <curlpp/Infos.hpp>

namespace asyncnet {
	AsyncSession::AsyncSession() : requestor_(AsyncRequestor::make_shared()) {
		initialize_session();
	}

	AsyncSession::AsyncSession(std::shared_ptr<AsyncRequestor> requestor) : requestor_(std::move(requestor)) {
		initialize_session();
	}

	void AsyncSession::initialize_session() {
		base_request_.set_share(std::make_shared<CurlShared>());
		base_request_.get_share()->set_cookies_shared(true);
		set_cookie_file(std::string(Request::cookie_memory));
	}

	void AsyncSession::set_max_redirects(const std::optional<long>& max_redirects) {
		base_request_.set_max_redirects(max_redirects);
	}

	void AsyncSession::set_verbose(const bool& is_verbose) {
		base_request_.set_verbose(is_verbose);
	}

	void AsyncSession::set_timeout(const std::optional<std::chrono::system_clock::duration>& timeout) {
		base_request_.set_timeout(timeout);
	}

	void AsyncSession::set_default_headers(const std::list<std::string>& headers) {
		default_headers_ = headers;
		base_request_.set_headers(default_headers_);
	}

	void AsyncSession::add_default_header(const std::string& header) {
		default_headers_.push_back(header);
		base_request_.set_headers(default_headers_);
	}

	void AsyncSession::set_cookie_file(const std::string& filename) {
		base_request_.set_cookie_file(filename);
	}

	CancellingTask<Response> AsyncSession::perform_request(const Request& request) {
		return requestor_->perform_request(request);
	}

};
