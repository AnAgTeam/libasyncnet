#pragma once
#include <exception>
#include <stdexcept>
#include <curlpp/Exception.hpp>

namespace asyncnet {

	/// Code for timeout runtime error
	constexpr CURLcode TimeoutErrorCode = CURLE_OPERATION_TIMEDOUT;
	/// Code for cancelled runtime error
	constexpr CURLcode CancelledErrorCode = CURLE_ABORTED_BY_CALLBACK;

	/**
	 * Call @ref whatCode() to get error code
	 */
	using NetworkLogicError = curlpp::LibcurlLogicError;

	/**
	 * Call @ref whatCode() to get error code
	 */
	using NetworkRuntimeError = curlpp::LibcurlRuntimeError;
};