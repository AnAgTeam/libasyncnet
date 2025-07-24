#pragma once
#include <asyncnet/Response.hpp>
#include <coroutine>
#include <variant>
#include <stop_token>

namespace asyncnet {
	class NetworkTask;

	namespace detail {
		struct GetStopTokenTag {
			explicit GetStopTokenTag() = default;
		};

		class Promise {
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

			using coroutine_handle = std::coroutine_handle<Promise>;
			using Variant = std::variant<std::monostate, Response, std::exception_ptr>;

			Promise() noexcept = default;
			Promise(const Promise& other) = delete;
			Promise(Promise&& other) = delete;
			~Promise() = default;

			Promise& operator=(const Promise& other) = delete;
			Promise& operator=(Promise&& other) = delete;

			NetworkTask get_return_object() noexcept;

			std::suspend_always initial_suspend() noexcept;

			FinalAwaitable final_suspend() noexcept;

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

			void unhandled_exception() noexcept;

			void continuation(std::coroutine_handle<> continuation) noexcept;

			void return_value(Response&& response);

			Response& result()&;
			const Response& result() const&;
			Response&& result()&&;

			bool request_stop() noexcept;

		protected:
			std::coroutine_handle<> continuation_ = nullptr;

		private:
			Variant storage_ = std::monostate{};
			std::stop_source stop_source_;
		};
	};

	class NetworkTask {
	public:
		using promise_type = detail::Promise;
		using coroutine_handle = std::coroutine_handle<promise_type>;

		struct Awaitable
		{
			Awaitable(coroutine_handle coroutine) noexcept;

			bool await_ready() const noexcept;

			std::coroutine_handle<> await_suspend(std::coroutine_handle<> coroutine) noexcept;

			std::coroutine_handle<promise_type> coroutine_ = nullptr;
		};

		/**
		 * co_await this field inside NetworkTask coroutine to get its @ref std::stop_token
		 */
		static constexpr detail::GetStopTokenTag get_stop_token {};

		NetworkTask() noexcept = default;
		NetworkTask(coroutine_handle coroutine) noexcept;
		NetworkTask(const NetworkTask& other) = delete;
		NetworkTask(NetworkTask&& other) noexcept;
		~NetworkTask() noexcept;

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
				decltype(auto) await_resume() const {
					return std::move(coroutine_.promise().result());
				}
			};

			return TaskAwaitable{ coroutine_ };
		}

		promise_type& promise() & noexcept;
		const promise_type& promise() const& noexcept;
		promise_type&& promise() && noexcept;

		coroutine_handle handle();

		/**
		 * Request coroutine to safe stop
		 */
		bool request_stop() noexcept;

	private:
		coroutine_handle coroutine_ = nullptr;
	};
	
};