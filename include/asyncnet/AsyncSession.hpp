#pragma once
#include <asyncnet/NetTypes.hpp>
#include <asyncnet/RequestPerformer.hpp>
#include <asyncnet/Request.hpp>

#include <list>
#include <vector>
#include <string>
#include <curlpp/cURLpp.hpp>
#include <curlpp/Easy.hpp>
#include <coro/task.hpp>

namespace asyncnet {

	class AsyncSession {
	public:

		/**
		 * Initialize asyncronous session. By default requestor is @ref Requestor.
		 * Shares cookies with all requests created from session.
		 */
		AsyncSession();

		/**
		 * Initialize asyncronous session with user's requestor
		 * Doesn't support multithreaded requestors.
		 * @throw RuntimeError if the requestor is multithreaded
		 * @see Requestor
		 * @param requestor Requestor which will perform all the session requests.
		 */
		explicit AsyncSession(std::shared_ptr<RequestPerformer> requestor);
		AsyncSession(const AsyncSession& other) = default;
		AsyncSession(AsyncSession&& other) = default;
		~AsyncSession() = default;

		/// @copydoc Request::set_max_redirects(max_redirects)
		void set_max_redirects(const std::optional<long>& max_redirects);

		/// @copydoc Request::set_verbose(is_verbose)
		void set_verbose(const bool& is_verbose);

		/// @copydoc Request::set_timeout(timeout)
		void set_timeout(const std::optional<std::chrono::system_clock::duration>& timeout);

		/// @copydoc Request::set_headers(headers)
		void set_default_headers(const std::list<std::string>& headers);

		/// @todo add doc
		void add_default_header(const std::string& header);

		/// @copydoc Request::set_cookie_file(filename)
		void set_cookie_file(const std::string& filename);

		/// @copydoc Request::set_option(arg)
		template<typename Option, typename OptionArg>
		void set_option(OptionArg&& arg) {
			base_request_.set_option<Option>(std::forward<OptionArg>(arg));
		}

		/// @copydoc Request::get_option()
		template<typename Option>
		auto get_option() {
			return base_request_.get_option<Option>();
		}

		/**
		 * Set the session requestor which will perform all requests.
		 * Doesn't support multithreaded requestors.
		 * @note It doesn't cancel all the previous requests and only affects future requests
		 * @throw RuntimeError if the requestor is multithreaded
		 * @param requestor The requestor to set
		 */
		void set_requestor(std::shared_ptr<RequestPerformer> requestor);

		/**
		 * Creates request which inherits all options from AsyncSession request
		 * @tparam T The request to create
		 * @tparam Args... Parameters, passed to the request's constructor
		 * @param args... Parameters, passed to the request's constructor
		 */
		template<std::derived_from<Request> T, typename ... Args>
		T make_request(Args&& ... args) const {
			return T(base_request_, std::forward<Args>(args) ...);
		}

		/**
		 * @brief perform request. Calls the bound requestor's perform_request()
		 * If timedout the @ref NetworkRuntimeError code will be @ref TimeoutErrorCode, if cancelled the code will be @ref CancelledErrorCode.
		 * Use task @see NetworkTask::request_stop() to cancel the request.
		 * @see Requestor
		 * @param request The request to perform asyncronously
		 * @return Awaitable task returning @see Response from request
		 * @throws NetworkRuntimeError If any runtime error
		 * @throws NetworkLogicError If any logic error
		 */
		NetworkTask<Response> perform_request(const Request& request);

	private:
		void initialize_session();

		void update_is_multithreaded();

		std::shared_ptr<RequestPerformer> requestor_;
		Request base_request_;
		std::list<std::string> default_headers_;
	};

}

