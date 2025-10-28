#pragma once
#include <asyncnet/CancellingTask.hpp>
#include <asyncnet/Response.hpp>

namespace asyncnet {
	struct SessionRequestor {
		virtual ~SessionRequestor() = default;

		virtual [[nodiscard]] CancellingTask<Response> perform_handle(curlpp::Easy handle) = 0;
		virtual [[nodiscard]] CancellingTask<Response> perform_request(const Request& request) = 0;
	};
}