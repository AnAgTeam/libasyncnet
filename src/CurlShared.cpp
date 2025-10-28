#include <asyncnet/CurlShared.hpp>

#include <curlpp/Easy.hpp>
#include <curl/curl.h>

namespace asyncnet {
	CurlShared::CurlShared() : handle_(curl_share_init()) {
		curlpp::runtimeAssert("Error when trying to curl_share_init() a handle", handle_ != nullptr);
	}

	CurlShared::~CurlShared() {
		curl_share_cleanup(handle_);
	}

	CurlShared::CurlShared(CurlShared&& other) noexcept :
		handle_(std::exchange(other.handle_, nullptr)),
		lock_function_(std::move(other.lock_function_)),
		unlock_function_(std::move(other.unlock_function_))
	{
	}

	CurlShared& CurlShared::operator=(CurlShared&& other) noexcept {
		if (std::addressof(other) != this) {
			handle_ = std::exchange(other.handle_, nullptr);
			lock_function_ = std::move(other.lock_function_);
			unlock_function_ = std::move(other.unlock_function_);
		}
		return *this;
	}

	void CurlShared::set_cookies_shared(bool shared) {
		set_share_option(CURL_LOCK_DATA_COOKIE, shared);
	}

	void CurlShared::set_dns_shared(bool shared) {
		set_share_option(CURL_LOCK_DATA_DNS, shared);
	}

	void CurlShared::set_ssl_shared(bool shared) {
		set_share_option(CURL_LOCK_DATA_SSL_SESSION, shared);
	}

	void CurlShared::set_connect_shared(bool shared) {
		set_share_option(CURL_LOCK_DATA_CONNECT, shared);
	}

	void CurlShared::set_psl_shared(bool shared) {
		set_share_option(CURL_LOCK_DATA_PSL, shared);
	}

	void CurlShared::set_hsts_shared(bool shared) {
		set_share_option(CURL_LOCK_DATA_HSTS, shared);
	}

	void CurlShared::set_lock_function(LockFunction lock_function) {
		curl_share_setopt(handle_, CURLSHOPT_LOCKFUNC, lock_function ? &CurlShared::lock_curl_function : nullptr);
		lock_function_ = std::move(lock_function); 
	}

	void CurlShared::set_unlock_function(UnlockFunction unlock_function) {
		curl_share_setopt(handle_, CURLSHOPT_UNLOCKFUNC, unlock_function ? &CurlShared::unlock_curl_function : nullptr);
		unlock_function_ = std::move(unlock_function);
	}

	void CurlShared::set_share_option(long type, bool shared) {
		CURLSHcode code = curl_share_setopt(handle_, shared ? CURLSHOPT_SHARE : CURLSHOPT_UNSHARE, type);
		// todo: check for error
	}

	CURLSH* CurlShared::get_handle() const noexcept {
		return handle_;
	}

	void CurlShared::add_handle(curlpp::Easy& easy_handle) const noexcept {
		curl_easy_setopt(easy_handle.getHandle(), CURLOPT_SHARE, handle_);
	}

	void CurlShared::remove_handle(curlpp::Easy& easy_handle) const noexcept {
		curl_easy_setopt(easy_handle.getHandle(), CURLOPT_SHARE, nullptr);
	}

	void CurlShared::lock_curl_function(CURL* easy_handle, curl_lock_data lock_data, curl_lock_access access, void* user_data) {
		CurlShared* context = static_cast<CurlShared*>(user_data);
		context->lock_function_(easy_handle, lock_data, access);
	}

	void CurlShared::unlock_curl_function(CURL* easy_handle, curl_lock_data lock_data, void* user_data) {
		CurlShared* context = static_cast<CurlShared*>(user_data);
		context->unlock_function_(easy_handle, lock_data);
	}

}