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
		[[nodiscard]] coro::task<int> yield(int timeout_ms);

		/**
		 * Cancel all pending requests with CURLE_ABORTED_BY_CALLBACK error and clear queue.
		 */
		[[nodiscard]] coro::task<void> cleanup();

		/**
		 * Cancel all pending requests with CURLE_ABORTED_BY_CALLBACK error and clear queue.
		 */
		[[nodiscard]] coro::task<void> abort_all();

		/**
		 * @brief perform curl easy handle.
		 * Pushes the request to multi interface and await's for it to finish.
		 * All the requests are executed in @see yield().
		 * If timedout the @ref NetworkRuntimeError code will be @ref TimeoutErrorCode, if cancelled the code will be @ref CancelledErrorCode.
		 * Use task @see CancellingTask::request_stop() to cancel the request.
		 * @note It sets @ref WriteStream option to it's own buffer, ignoring user buffer. It sets @ref ProgressFunction and @ref NoProgress options, ignoring user's
		 * @param handle The handle to execute asyncronously
		 * @return Awaitable task returning @see Response from request
		 * @throws NetworkRuntimeError If any runtime error
		 * @throws NetworkLogicError If any logic error
		 */
		[[nodiscard]] NetworkTask<Response> perform_handle(curlpp::Easy handle);

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
		};

		CURLM* handle_;
		mutable coro::mutex mutex_;
		std::map<CURL*, HandleAwaiterContext> condition_awaiters_;
	};
}