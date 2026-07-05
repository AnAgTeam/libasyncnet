#include <asyncnet/Response.hpp>

namespace asyncnet{
	Response::Response(curlpp::Easy&& handle, std::string header_block)
		: handle_(std::move(handle)), header_block_(std::move(header_block)) {

	}

	Response::Response(curlpp::Easy&& handle, std::ostringstream&& stream, std::string header_block)
		: handle_(std::move(handle)), stream_(std::move(stream)), header_block_(std::move(header_block)) {

	}

	long Response::get_status_code() const {
		long status;
		handle_.getCurlHandle().getInfo(CURLINFO_RESPONSE_CODE, status);
		return status;
	}

	std::string Response::get_text() const & {
		if (stream_) {
			return stream_->str();
		}
		else {
			return "";
		}
	}

	std::string Response::get_text() && {
		if (stream_) {
			return std::move(*stream_).str();
		}
		else {
			return "";
		}
	}

	const std::string& Response::get_header_block() const & {
		return header_block_;
	}

	std::string Response::get_header_block() && {
		return std::move(header_block_);
	}

	std::string Response::get_effective_url() const {
		// curl returns CURLINFO_EFFECTIVE_URL as a char*, so getInfo must receive a
		// char*& (a char** for curl). Passing a std::string& makes curl write the
		// pointer over the string's internals and corrupt the heap; copy the C
		// string out instead.
		char* url = nullptr;
		handle_.getCurlHandle().getInfo(CURLINFO_EFFECTIVE_URL, url);
		return url ? std::string(url) : std::string();
	}
}