#pragma once
#include <curlpp/Option.hpp>

namespace asyncnet {

	class CurlShared {
	public:

		/**
		 * Mutex lock callback. Called when libcurl wants to lock the mutex.
		 * Used for managing concurrency.
		 * @see set_lock_function
		 */
		using LockFunction = std::function<void(CURL*, curl_lock_data, curl_lock_access)>;

		/**
		 * Mutex unlock callback. Called when libcurl wants to unlock the mutex.
		 * Used for managing concurrency.
		 * @see set_unlock_function
		 */
		using UnlockFunction = std::function<void(CURL*, curl_lock_data)>;

		/**
		 * Initializes libcurl share handle.
		 * @throws RuntimeError If libcurl initialization failed
		 */
		CurlShared();

		CurlShared(const CurlShared& other) noexcept = delete;

		/**
		 * Effectivly move libcurl share handle and mutex functions.
		 * @param other The CurlShare to move
		 */
		CurlShared(CurlShared&& other) noexcept;

		/**
		 * Cleanup libcurl share handle and functions.
		 */
		~CurlShared();

		CurlShared& operator=(const CurlShared& other) noexcept = delete;

		/**
		 * Effectivly move libcurl share handle and mutex functions.
		 * @param other The CurlShare to move share handle
		 */
		CurlShared& operator=(CurlShared&& other) noexcept;

		/**
		 * Cookie data is shared across the easy handles using this shared object.
		 * @note This does not activate an easy handle's cookie handling.
		 * @note It is not supported to share cookies between multiple concurrent threads. 
		 * @param shared True to enable cookies sharing, false otherwise
		 */
		void set_cookies_shared(bool shared);

		/**
		 * Cached DNS hosts are shared across the easy handles using this shared object.
		 * @note When you use the multi interface, all easy handles added to the same
		 *       multi handle share the DNS cache by default without using this option.
		 * @param shared True to enable dns sharing, false otherwise
		 */
		void set_dns_shared(bool shared);

		/**
		 * SSL sessions are shared across the easy handles using this shared object.
		 * This reduces the time spent in the SSL handshake when reconnecting to the same server.
		 * @note When you use the multi interface, all easy handles added to the same 
		 *       multi handle share the SSL session cache by default without using this option.
		 * @param shared True to enable SSL session sharing, false otherwise
		 */
		void set_ssl_shared(bool shared);

		/**
		 * Put the connection cache in the share object and make all easy handles using this share object share the connection cache.
		 * 
		 * Connections that are used for HTTP/2 or HTTP/3 multiplexing only get additional transfers added to them
		 * if the existing connection is held by the same multi or easy handle.
		 * libcurl does not support doing multiplexed streams in different threads using a shared connection.
		 * 
		 * @note When you use the multi interface, all easy handles added to the same
		 *       multi handle share the connection cache by default without using this option. 
		 * @note It is not supported to share connections between multiple concurrent threads.
		 * @param shared True to enable connect cache sharing, false otherwise
		 */
		void set_connect_shared(bool shared);

		/**
		 * The Public Suffix List stored in the share object is made available to all easy handle bound to the later.
		 * Since the Public Suffix List is periodically refreshed, this avoids updates in too many different contexts. 
		 * @note When you use the multi interface, all easy handles added to the same
		 *       multi handle share the PSL cache by default without using this option.
		 * @param shared True to enable PSL sharing, false otherwise
		 */
		void set_psl_shared(bool shared);

		/**
		 * The in-memory HSTS cache.
		 * @note It is not supported to share the HSTS between multiple concurrent threads. 
		 * @param shared True to enable HSTS sharing, false otherwise
		 */
		void set_hsts_shared(bool shared);

		/**
		 * Set the mutex lock callback for concurrency. @see LockFunction
		 * @param lock_function Lock callback
		 */
		void set_lock_function(LockFunction lock_function);

		/**
		 * Set the mutex unlock callback for concurrency. @see UnlockFunction
		 * @param unlock_function Unlock callback
		 */
		void set_unlock_function(UnlockFunction unlock_function);

		/**
		 * Get the libcurl share handle
		 * @return libcurl share handle
		 */
		[[nodiscard]] CURLSH* get_handle() const noexcept;

		/**
		 * Share easy handle data with this shared handle.
		 * @param easy_handle easy handle
		 */
		void add_handle(curlpp::Easy& easy_handle) const noexcept;

		/**
		 * Remove easy handle this shared handle.
		 * @param easy_handle easy handle
		 */
		void remove_handle(curlpp::Easy& easy_handle) const noexcept;

	private:

		/**
		 * libcurl share and unshare setter
		 * @param type Data type to share
		 * @param shared true if shared, false otherwise
		 */
		void set_share_option(long type, bool shared);

		/**
		 * Mutex lock function which calls the context lock function
		 */
		static void lock_curl_function(CURL* easy_handle, curl_lock_data lock_data, curl_lock_access access, void* user_data);

		/**
		 * Mutex unlock function which calls the context unlock function
		 */
		static void unlock_curl_function(CURL* easy_handle, curl_lock_data lock_data, void* user_data);

		CURLSH* handle_;
		LockFunction lock_function_;
		UnlockFunction unlock_function_;
	};

	//using CurlShareOption = curlpp::OptionTrait<CURLSH*, CURLOPT_SHARE>;
}