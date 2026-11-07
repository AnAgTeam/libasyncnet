#pragma once
#include <asyncnet/CurlMulti.hpp>
#include <asyncnet/Request.hpp>
#include <asyncnet/RequestPerformer.hpp>

#include <curlpp/Easy.hpp>
#include <thread>
#include <latch>
#include <mutex>

namespace asyncnet {

	class Requestor : CurlMulti, public RequestPerformer {
		struct private_constructor { explicit private_constructor() = default; };

	public:

		/**
		 * @see make_shared()
		 */
		Requestor(private_constructor);

		Requestor(const Requestor& other) = delete;
		Requestor(Requestor&& other) = delete;
		virtual ~Requestor();

		/**
		 * @brief perform curl easy handle.
		 * Pushes the request to multi interface and await's for it to finish.
		 * All the requests are executed in requestor's thread.
		 * If timedout the @ref NetworkRuntimeError code will be @ref TimeoutErrorCode, if cancelled the code will be @ref CancelledErrorCode.
		 * Use task @see CancellingTask::request_stop() to cancel the request.
		 * @note It sets @ref WriteStream option to it's own buffer, ignoring user buffer. It sets @ref ProgressFunction and @ref NoProgress options, ignoring user's
		 *		If the requestor is shutting down all the requests will automatically be cancelled.
		 *       On fatal polling failure, unfinished and future requests receive the saved
		 *       transport exception; already completed results are still delivered.
		 * @note After completion, the task will be executed in requestor's thread.
		 *       Keep this object alive until the task completes or is destroyed.
		 * @param handle The handle to execute asyncronously
		 * @return Awaitable task returning @see Response from request
		 * @throws NetworkRuntimeError If any runtime error
		 * @throws NetworkLogicError If any logic error
		 * @throws RuntimeError If trying to perform handle while requestor was shut down
		 */
		virtual NetworkTask<Response> perform_handle(curlpp::Easy handle) override;

		/**
		 * @brief Perform a request on the polling thread.
		 * @param request Request whose configuration is used to create the easy handle.
		 * @return Task yielding the response.
		 * @note Keep this object and request alive until the task completes or is destroyed.
		 */
		virtual NetworkTask<Response> perform_request(const Request& request) override;

		virtual bool is_multithreaded() override;

		/**
		 * @brief Stop accepting requests and cancel pending transfers.
		 * Calls from outside the worker wait for it to exit; a call from a continuation
		 * on the worker requests shutdown and returns without joining itself.
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
		 * @param shared_this Shared ownership of this Requestor, kept alive for the
		 *        executor's lifetime; when it becomes the sole owner the executor shuts down
		 */
		void yield_executor(std::shared_ptr<Requestor> shared_this);

		// Also bounds how long automatic last-owner shutdown can take.
		static constexpr int poll_timeout_ms = 1000;
		std::latch started_{1};
		std::thread::id worker_id_;
		std::mutex join_mutex_;
		std::atomic_bool shutting_down_;
		std::thread yield_thread_;
	};
}