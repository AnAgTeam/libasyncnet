#pragma once
#include <asyncnet/NetTypes.hpp>
#include <asyncnet/Requestor.hpp>
#include <asyncnet/Request.hpp>

#include <list>
#include <vector>
#include <string>
#include <curlpp/cURLpp.hpp>
#include <curlpp/Easy.hpp>
#include <coro/task.hpp>

namespace asyncnet {

	class AsyncSession : Requestor {
	public:
		explicit AsyncSession(const unsigned worker_count);
		explicit AsyncSession(Requestor&& requestor);
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

		/**
		 * Creates request which inherits all options from AsyncSession request
		 * @tparam T The request to create
		 * @tparam Args... Parameters, passed to the request's constructor
		 * @param args... Parameters, passed to the request's constructor
		 */
		template<std::derived_from<Request> T, typename ... Args>
		T make_request(Args&& ... args) {
			return T(base_request_, std::forward<Args>(args) ...);
		}

		using Requestor::perform_request;

	private:
		void initialize_handle();

		Request base_request_;
		std::list<std::string> default_headers_;
	};

}

