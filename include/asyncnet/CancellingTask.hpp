#pragma once
#include <asyncnet/Response.hpp>
#include <coroutine>
#include <variant>
#include <stop_token>

/*
	Big thanks to developers of libcoro!! <3
	Almost fully rewritten from (https://github.com/jbaldwin/libcoro/blob/main/include/coro/task.hpp)
*/

namespace asyncnet {

	template<typename T>
	class CancellingTask;

	namespace detail {
		struct GetStopTokenTag {
			explicit GetStopTokenTag() = default;
		};

		class BasicPromise {
		public:
			struct FinalAwaitable {
				bool await_ready() const noexcept {
					return false;
				}

				template<typename T>
				std::coroutine_handle<> await_suspend(std::coroutine_handle<T> coroutine) noexcept {
					auto& promise = coroutine.promise();
					if (promise.continuation_) {
						return promise.continuation_;
					}
					else {
						return std::noop_coroutine();
					}
				}

				void await_resume() noexcept {

				}
			};

			BasicPromise() noexcept = default;
			BasicPromise(const BasicPromise& other) = delete;
			BasicPromise(BasicPromise&& other) = delete;
			~BasicPromise() = default;

			BasicPromise& operator=(const BasicPromise& other) = delete;
			BasicPromise& operator=(BasicPromise&& other) = delete;

			std::suspend_always initial_suspend() noexcept {
				return {};
			}

			FinalAwaitable final_suspend() noexcept {
				return {};
			}

			auto await_transform(GetStopTokenTag) noexcept {
				struct Awaiter : std::suspend_never {
					std::stop_source& stop_source;

					std::stop_token await_resume() const noexcept {
						return stop_source.get_token();
					}
				};

				return Awaiter{ {}, stop_source_ };
			}

			template<typename T>
			decltype(auto) await_transform(T&& coroutine) noexcept {
				return std::forward<T>(coroutine);
			}

			void continuation(std::coroutine_handle<> coroutine) noexcept {
				continuation_ = coroutine;
			}

			bool request_stop() noexcept {
				return stop_source_.request_stop();
			}

		protected:
			std::coroutine_handle<> continuation_ = nullptr;
			std::stop_source stop_source_;
		};

		template<typename T>
		class Promise : public BasicPromise {
		public:
			static constexpr bool return_is_reference = std::is_reference_v<T>;

			using coroutine_handle = std::coroutine_handle<Promise<T>>;
			using stored_type = std::conditional_t<return_is_reference,
				std::remove_reference_t<T>*,
				std::remove_const_t<T>>;
			using variant = std::variant<std::monostate, stored_type, std::exception_ptr>;

			Promise() noexcept = default;
			Promise(const Promise& other) = delete;
			Promise(Promise&& other) = delete;
			~Promise() = default;

			Promise& operator=(const Promise& other) = delete;
			Promise operator=(Promise&& other) = delete;

			CancellingTask<T> get_return_object() noexcept;

			void unhandled_exception() noexcept {
				storage_ = std::current_exception();
			}

			template<typename U>
				requires ((return_is_reference && std::is_constructible_v<T, U&&>) ||
					(!return_is_reference && std::is_constructible_v<stored_type, U&&>))
			void return_value(U&& value) {
				if constexpr (return_is_reference) {
					T ref = static_cast<U&&>(value);
					storage_.emplace<stored_type>(std::addressof(ref));
				} else {
					storage_.emplace<stored_type>(std::forward<U>(value));
				}
			}

			void return_value(stored_type&& value) requires (!return_is_reference) {
				if constexpr (std::is_move_constructible_v<stored_type>) {
					storage_.emplace<stored_type>(std::move(value));
				}
				else {
					storage_.emplace<stored_type>(value);
				}
			}

			decltype(auto) result() & {
				if (std::holds_alternative<stored_type>(storage_)) {
					if constexpr (return_is_reference) {
						return static_cast<T>(*std::get<stored_type>(storage_));
					}
					else {
						return static_cast<const T&>(std::get<stored_type>(storage_));
					}
				}
				else if (std::holds_alternative<std::exception_ptr>(storage_)) {
					std::rethrow_exception(std::get<std::exception_ptr>(storage_));
				}
				else {
					throw std::runtime_error("Promise value is unsetted");
				}
			}

			decltype(auto) result() const& {
				if (std::holds_alternative<stored_type>(storage_)) {
					if constexpr (return_is_reference) {
						return static_cast<std::add_const_t<T>>(*std::get<stored_type>(storage_));
					}
					else {
						return static_cast<const T&>(std::get<stored_type>(storage_));
					}
				}
				else if (std::holds_alternative<std::exception_ptr>(storage_)) {
					std::rethrow_exception(std::get<std::exception_ptr>(storage_));
				}
				else {
					throw std::runtime_error("Promise value is unsetted");
				}
			}

			decltype(auto) result() && {
				if (std::holds_alternative<stored_type>(storage_)) {
					if constexpr (return_is_reference) {
						return static_cast<T>(*std::get<stored_type>(storage_));
					}
					else {
						return static_cast<T&&>(std::get<stored_type>(storage_));
					}
				}
				else if (std::holds_alternative<std::exception_ptr>(storage_)) {
					std::rethrow_exception(std::get<std::exception_ptr>(storage_));
				}
				else {
					throw std::runtime_error("Promise value is unsetted");
				}
			}

		private:
			variant storage_ = std::monostate{};
		};

		template<>
		class Promise<void> : public BasicPromise {
		public:
			using coroutine_handle = std::coroutine_handle<Promise<void>>;

			Promise() noexcept = default;
			Promise(const Promise& other) = delete;
			Promise(Promise&& other) = delete;
			~Promise() = default;

			Promise& operator=(const Promise& other) = delete;
			Promise operator=(Promise&& other) = delete;

			CancellingTask<void> get_return_object() noexcept;

			void unhandled_exception() noexcept {
				exception_ptr_ = std::current_exception();
			}

			void return_void() {}

			auto result() {
				if (exception_ptr_) {
					std::rethrow_exception(exception_ptr_);
				}
			}

		private:
			std::exception_ptr exception_ptr_ = nullptr;
		};
	};

	template<typename T>
	class CancellingTask {
	public:
		using promise_type = detail::Promise<T>;
		using coroutine_handle = std::coroutine_handle<promise_type>;

		struct Awaitable
		{
			Awaitable(coroutine_handle coroutine) noexcept : coroutine_(coroutine) {}

			bool await_ready() const noexcept {
				return !coroutine_ || coroutine_.done();
			}

			std::coroutine_handle<> await_suspend(std::coroutine_handle<> coroutine) noexcept {
				coroutine_.promise().continuation(coroutine);
				return coroutine_;
			}

			std::coroutine_handle<promise_type> coroutine_ = nullptr;
		};

		/**
		 * co_await this field inside NetworkTask coroutine to get its @ref std::stop_token
		 */
		static constexpr detail::GetStopTokenTag get_stop_token {};

		CancellingTask() noexcept = default;
		CancellingTask(coroutine_handle coroutine) noexcept : coroutine_(coroutine) {}
		CancellingTask(const CancellingTask& other) = delete;
		CancellingTask(CancellingTask&& other) noexcept : coroutine_(std::exchange(other.coroutine_, nullptr)) {}
		~CancellingTask() noexcept = default;

		auto operator co_await() const& noexcept {
			struct TaskAwaitable : Awaitable {
				decltype(auto) await_resume() const {
					return coroutine_.promise().result();
				}
			};

			return TaskAwaitable{ coroutine_ };
		}

		auto operator co_await() const && noexcept {
			struct TaskAwaitable : Awaitable {
				decltype(auto) await_resume() {
					return std::move(coroutine_.promise()).result();
				}
			};

			return TaskAwaitable{ coroutine_ };
		}

		promise_type& promise() & noexcept {
			return coroutine_.promise();
		}

		const promise_type& promise() const& noexcept {
			return coroutine_.promise();
		}

		promise_type&& promise() && noexcept {
			return std::move(coroutine_.promise());
		}

		coroutine_handle handle() {
			return coroutine_;
		}

		/**
		 * Request coroutine to safe stop
		 */
		bool request_stop() noexcept {
			return coroutine_.promise().request_stop();
		}

	private:
		coroutine_handle coroutine_ = nullptr;
	};
	

	namespace detail {
		template<typename T>
		CancellingTask<T> Promise<T>::get_return_object() noexcept {
			return CancellingTask<T>{ coroutine_handle::from_promise(*this) };
		}
	}
};