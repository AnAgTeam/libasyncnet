#pragma once
#include <asyncnet/CurlMulti.hpp>
#include <asyncnet/Request.hpp>
#include <asyncnet/RequestPerformer.hpp>

#include <curlpp/Easy.hpp>
#include <thread>

namespace asyncnet {

	class Requestor : CurlMulti, public RequestPerformer, public std::enable_shared_from_this<Requestor> {
		struct private_constructor { explicit private_constructor() = default; };

	public:

		/**
		 * @see make_shared()
		 */
		Requestor(private_constructor);

		Requestor(const Requestor& other) = delete;
		Requestor(Requestor&& other) = default;
		virtual ~Requestor();

		/**
		 * @brief perform curl easy handle.
		 * Pushes the request to multi interface and await's for it to finish.
		 * All the requests are executed in requestor's thread.
		 * If timedout the @ref NetworkRuntimeError code will be @ref TimeoutErrorCode, if cancelled the code will be @ref CancelledErrorCode.
		 * Use task @see CancellingTask::request_stop() to cancel the request.
		 * @note It sets @ref WriteStream option to it's own buffer, ignoring user buffer. It sets @ref ProgressFunction and @ref NoProgress options, ignoring user's
		 *		If the requestor is shutting down all the requests will automatically be cancelled
		 * @note After completion, the task will be executed in requestor's thread
		 * @param handle The handle to execute asyncronously
		 * @return Awaitable task returning @see Response from request
		 * @throws NetworkRuntimeError If any runtime error
		 * @throws NetworkLogicError If any logic error
		 * @throws RuntimeError If trying to perform handle while requestor was shut down
		 */
		virtual [[nodiscard]] CancellingTask<Response> perform_handle(curlpp::Easy handle) override;

		/** 
		 * 
		 */
		virtual [[nodiscard]] CancellingTask<Response> perform_request(const Request& request) override;

		virtual bool is_multithreaded() override;

		/**
		 * Prevent handle adding and cancel all the pending requests with CURLE_ABORTED_BY_CALLBACK
		 */
		void shutdown();

		/**
		 * Constructs AsyncRequestor and creates a new thread which performs all the passed requests
		 * @return Pointer to created requestor
		 */
		static std::shared_ptr<Requestor> make_shared();

	private:

		/**
		 * Requestor's thread body function
		 */
		coro::task<void> yield_executor();

		int timeout_ms_;
		std::atomic_bool shutting_down_;
		std::thread yield_thread_;
	};
}