#pragma once
#include <asyncnet/CurlMulti.hpp>
#include <asyncnet/Request.hpp>
#include <asyncnet/RequestPerformer.hpp>

#include <curlpp/Easy.hpp>
#include <thread>

namespace asyncnet {

	class Requestor : CurlMulti, public RequestPerformer {
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
		virtual NetworkTask<Response> perform_handle(curlpp::Easy handle) override;

		/** 
		 * 
		 */
		virtual NetworkTask<Response> perform_request(const Request& request) override;

		virtual bool is_multithreaded() override;

		/**
		 * Prevent handle adding and cancel all the pending requests with CURLE_ABORTED_BY_CALLBACK.
		 * After this, no new work may be submitted: perform_handle()/perform_request()
		 * assert in debug and throw RuntimeError in release. Test with running().
		 * @note Asynchronous: signals the worker and returns; the worker stops and tears
		 *       down shortly after. The object stays alive while any handle from
		 *       make_shared() is held.
		 */
		void shutdown();

		/**
		 * @return true while the requestor still accepts work; false once shutdown()
		 *         has begun. Cheap to poll before submitting.
		 */
		[[nodiscard]] bool running() const noexcept;

		/**
		 * Constructs AsyncRequestor and creates a new thread which performs all the passed requests
		 * @return Pointer to created requestor
		 */
		static std::shared_ptr<Requestor> make_shared();

	private:

		/**
		 * Requestor's thread body function.
		 * @note STATIC on purpose: the coroutine frame carries no implicit `this`, so the
		 *       only handle to the object is @p self. *self is destroyed at the explicit
		 *       self.reset() at the end of the body — nothing may touch the object past it.
		 * @param self The worker's owning (deleting) reference, kept alive for the
		 *        executor's lifetime. Shutdown is event-driven (the public handle's
		 *        deleter sets the flag and wakes the poll), not detected by use_count.
		 */
		static coro::task<void> yield_executor(std::shared_ptr<Requestor> self);

		int timeout_ms_;
		std::atomic_bool shutting_down_;
		std::thread yield_thread_;
	};
}