#pragma once
#include <asyncnet/NetTypes.hpp>
#include <asyncnet/NetworkRequestor.hpp>

#include <list>
#include <vector>
#include <string>
#include <curlpp/cURLpp.hpp>
#include <curlpp/Easy.hpp>
#include <coro/task.hpp>

namespace asyncnet {

	class AsyncNetworkSession : public NetworkRequestor {
	public:
		static const std::string_view cookie_memory;

		static void init();
		static void deinit();

		explicit AsyncNetworkSession(const unsigned worker_count);
		explicit AsyncNetworkSession(NetworkRequestor&& requestor);
		AsyncNetworkSession(const AsyncNetworkSession& other) = default;
		AsyncNetworkSession(AsyncNetworkSession&& other) = default;
		~AsyncNetworkSession() = default;

		void enable_redirects(const bool value, const long max);
		void set_verbose(const bool value);
		void set_timeout(const long value);
		void set_default_headers(const std::list<std::string>& headers);
		void add_default_header(const std::string& header);
		void set_cookie_file(const std::string& filename);

		coro::task<std::string> get_request(
			const std::string url,
			const std::vector<std::string> extra_headers = {},
			const UrlParameters params = UrlParameters(),
			std::optional<std::stop_token> stop_source = std::nullopt
		) const;

		coro::task<std::string> post_request(
			const std::string url,
			const std::string data,
			std::string_view content_type,
			const std::vector<std::string> extra_headers = {},
			const UrlParameters params = UrlParameters(),
			std::optional<std::stop_token> stop_source = std::nullopt
		) const;

		coro::task<std::string> post_multipart_request(const std::string url, const MultipartForms forms, const std::vector<std::string> extra_headers = {}, const UrlParameters params = UrlParameters()) const;

	private:
		void initialize_handle();

		std::list<std::string> make_expanded_header(const std::vector<std::string>& extra_headers) const;

		curlpp::Easy base_handle_;
		std::list<std::string> default_headers_;
	};

}

