#include <asyncnet/Response.hpp>

namespace asyncnet{
	Response::Response(curlpp::Easy handle) : handle_(std::move(handle)) {

	}

	Response::Response(curlpp::Easy handle, std::ostringstream&& stream) : handle_(std::move(handle)), stream_(std::move(stream)) {

	}

	long Response::get_status_code() const {
		long status;
		handle_.getCurlHandle().getInfo(CURLINFO_RESPONSE_CODE, status);
		return status;
	}

	std::string Response::get_text() const {
		if (stream_) {
			return stream_->str();
		}
		else {
			return "";
		}
	}
}