#pragma once
#include <asyncnet/CancellingTask.hpp>
#include <asyncnet/Response.hpp>

#include <curl/multi.h>
#include <coro/condition_variable.hpp>
#include <memory>
#include <map>
#include <optional>

namespace asyncnet {
	class CurlMulti {
	public:

		/**
		 * Initializes libcurl multi handle.
		 * To add requests use @see perform_handle(). To execute requests use @see yield()
		 * @throws RuntimeError If libcurl initialization failed
		 */
		CurlMulti();

		/**
		 * Initialize from passed libcurl multi handle
		 * @param handle libcurl multi handle
		 */
		explicit CurlMulti(CURLM* handle) noexcept;
		CurlMulti(const CurlMulti& other) = delete;
		CurlMulti(CurlMulti&& other) noexcept;

		/**
		 * Cleanup the multi handle.
		 * @note All the pending tasks will be cancelled and run syncronously. To cleanup asyncronously use @see cleanup
		 * @throw RuntimeError If has pending handles in the queue
		 */
		~CurlMulti() noexcept;

		CurlMulti& operator=(const CurlMulti& other) = delete;

		/**
		 * Effectively move multi inteface and queue 
		 * @note All the pending tasks will be cancelled and ran syncronously. To cleanup asyncronously use @see cleanup
		 */
		CurlMulti& operator=(CurlMulti&& other) noexcept;

		/**
		 * Process all the pending requests in the queue.
		 * Returns if timeout exceeded, has empty queue or a new request has pushed by perform_handle().
		 * Should be called constantly to actually process requests.
		 * Internally calls @ref curl_multi_perform() and @ref curl_multi_poll()
		 * @param timeout_ms Timeout of pull in milliseconds
		 * @return Currently active requests in the queue
		 */
		coro::task<int> yield(int timeout_ms);

		/**
		 * Deliver the exit code of every transfer curl has finished to the coroutine
		 * awaiting it, and remove the handle from the multi.
		 * @return Number of transfers completed
		 */
		int deliver_finished();

		/**
		 * Abort every in-flight transfer whose stop was requested, without waiting for
		 * curl to notice.
		 * curl only observes a stop from the xferinfo callback, which it does not run
		 * while connecting or doing the TLS handshake — so a cancel arriving in that
		 * window would otherwise wait out the whole connect. Ending them here makes
		 * cancellation immediate regardless of which phase the transfer is in.
		 * @return Number of transfers aborted
		 */
		int abort_cancelled();

		/**
		 * Cancel all pending requests with CURLE_ABORTED_BY_CALLBACK error and clear queue.
		 */
		coro::task<void> cleanup();

		/**
		 * Cancel all pending requests with CURLE_ABORTED_BY_CALLBACK error and clear queue.
		 */
		coro::task<void> abort_all();

		/**
		 * @brief perform curl easy handle.
		 * Pushes the request to multi interface and await's for it to finish.
		 * All the requests are executed in @see yield().
		 * If timedout the @ref NetworkRuntimeError code will be @ref TimeoutErrorCode, if cancelled the code will be @ref CancelledErrorCode.
		 * Use task @see CancellingTask::request_stop() to cancel the request.
		 * @note If @p output_stream is nullptr it sets @ref WriteStream option to its own buffer,
		 *		ignoring user buffer, and the body is available via @ref Response::get_text().
		 *		Otherwise the body is streamed into @p output_stream (which must outlive the task)
		 *		and @ref Response::get_text() returns empty. It sets @ref ProgressFunction and
		 *		@ref NoProgress options, ignoring user's
		 * @param handle The handle to execute asyncronously
		 * @param output_stream Optional caller-owned stream to receive the body, or nullptr to buffer internally
		 * @return Awaitable task returning @see Response from request
		 * @throws NetworkRuntimeError If any runtime error
		 * @throws NetworkLogicError If any logic error
		 */
		NetworkTask<Response> perform_handle(curlpp::Easy handle, std::ostream* output_stream = nullptr);

		/**
		 * Get libcurl multi interface handle
		 * @return libcurl multi handle
		 */
		[[nodiscard]] CURLM* get_handle() const noexcept;

		/**
		 * Wakeup thread which is polling for events in yield() and exit immediately from yield()
		 * Used when a new request is pushed, but already polling or to update poll timeout
		 */
		void wakeup_polling() const;

	private:
		struct HandleAwaiterContext {
			coro::condition_variable cv;
			std::optional<CURLcode> exit_code;
			/// The awaiting request's promise, borrowed — it lives on that coroutine's
			/// frame, which outlives its entry here. Only read to ask whether a stop was
			/// requested; @see abort_cancelled().
			NetworkPromise<Response>* promise = nullptr;
		};

		CURLM* handle_;
		mutable coro::mutex mutex_;
		std::map<CURL*, HandleAwaiterContext> condition_awaiters_;
	};
}