#pragma once
#include <asyncnet/CancellingTask.hpp>
#include <asyncnet/Response.hpp>
#include <asyncnet/Request.hpp>

namespace asyncnet {
	struct RequestPerformer {
		virtual ~RequestPerformer() = default;

		virtual [[nodiscard]] NetworkTask<Response> perform_handle(curlpp::Easy handle) = 0;
		virtual [[nodiscard]] NetworkTask<Response> perform_request(const Request& request) = 0;

		virtual bool is_multithreaded() = 0;
	};
}