#pragma once
#include <asyncnet/utility/Concepts.hpp>
#include <stdexcept>
#include <coroutine>
#include <variant>
#include <stop_token>

/*
	Big thanks to developers of libcoro!! <3
	Almost fully rewritten from (https://github.com/jbaldwin/libcoro/blob/main/include/coro/task.hpp)
*/

namespace asyncnet {

	template<typename T>
	class Promise;

	template<typename T>
	class NetworkPromise;

	template<typename T, std::derived_from<Promise<T>> TaskPromise = Promise<T>>
	class CancellingTask;

	template<typename T, std::derived_from<NetworkPromise<T>> TaskPromise = NetworkPromise<T>>
	class NetworkTask;

	namespace detail {
		struct GetStopTokenTag {
			explicit GetStopTokenTag() = default;
		};

		struct GetStopSourceTag {
			explicit GetStopSourceTag() = default;
		};

		struct GetPromiseTag {
			explicit GetPromiseTag() = default;
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

			auto await_transform(GetStopSourceTag) noexcept {
				struct Awaiter : std::suspend_never {
					std::stop_source& stop_source;

					std::stop_source await_resume() noexcept {
						return stop_source;
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

			bool stop_requested() const noexcept {
				return stop_source_.stop_requested();
			}

			void set_stop_source(std::stop_source stop_source) {
				stop_source_.swap(stop_source);
			}

			std::stop_source& stop_source() {
				return stop_source_;
			}

		protected:
			std::coroutine_handle<> continuation_ = nullptr;
			std::stop_source stop_source_;
		};
	};

	template<typename T>
	class Promise : public detail::BasicPromise {
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
			requires ((return_is_reference&& std::is_constructible_v<T, U&&>) ||
		(!return_is_reference && std::is_constructible_v<stored_type, U&&>))
			void return_value(U&& value) {
			if constexpr (return_is_reference) {
				T ref = static_cast<U&&>(value);
				storage_.template emplace<stored_type>(std::addressof(ref));
			}
			else {
				storage_.template emplace<stored_type>(std::forward<U>(value));
			}
		}

		void return_value(stored_type&& value) requires (!return_is_reference) {
			if constexpr (std::is_move_constructible_v<stored_type>) {
				storage_.template emplace<stored_type>(std::move(value));
			}
			else {
				storage_.template emplace<stored_type>(value);
			}
		}

		decltype(auto) result()& {
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

		decltype(auto) result()&& {
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
	class Promise<void> : public detail::BasicPromise {
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

	template<typename T>
	class NetworkPromise : public Promise<T> {
	public:
		using coroutine_handle = std::coroutine_handle<NetworkPromise<T>>;

		auto await_transform(detail::GetPromiseTag) noexcept {
			struct Awaiter : std::suspend_never {
				NetworkPromise& promise;

				NetworkPromise& await_resume() noexcept {
					return promise;
				}
			};

			return Awaiter{ {}, *this };
		}

		using Promise<T>::await_transform;

		const long long& read_bytes() const {
			if (std::holds_alternative<long long>(read_bytes_count_)) {
				return std::get<long long>(read_bytes_count_);
			}
			return *std::get<long long*>(read_bytes_count_);
		}

		const long long& total_bytes() const {
			if (std::holds_alternative<long long>(total_bytes_count_)) {
				return std::get<long long>(total_bytes_count_);
			}
			return *std::get<long long*>(total_bytes_count_);
		}

		long long& read_bytes() {
			if (std::holds_alternative<long long>(read_bytes_count_)) {
				return std::get<long long>(read_bytes_count_);
			}
			return *std::get<long long*>(read_bytes_count_);
		}

		long long& total_bytes() {
			if (std::holds_alternative<long long>(total_bytes_count_)) {
				return std::get<long long>(total_bytes_count_);
			}
			return *std::get<long long*>(total_bytes_count_);
		}

		void set_read_bytes(long long new_read_bytes) {
			if (std::holds_alternative<long long>(read_bytes_count_)) {
				std::get<long long>(read_bytes_count_) = new_read_bytes;
			}
			else {
				*std::get<long long*>(read_bytes_count_) = new_read_bytes;
			}
		}

		void set_total_bytes(long long new_total_bytes) {
			if (std::holds_alternative<long long>(total_bytes_count_)) {
				std::get<long long>(total_bytes_count_) = new_total_bytes;
			}
			else {
				*std::get<long long*>(total_bytes_count_) = new_total_bytes;
			}
		}

		NetworkTask<T> get_return_object() noexcept;

		template<typename U>
		void connect_with(NetworkPromise<U>& other_promise) {
			this->set_stop_source(other_promise.stop_source());

			read_bytes_count_ = std::addressof(other_promise.read_bytes());
			total_bytes_count_ = std::addressof(other_promise.total_bytes());
		}

	private:

		std::variant<long long, long long*> read_bytes_count_;
		std::variant<long long, long long*> total_bytes_count_;
	};

	namespace awaitables {
		/**
		 * co_await this field inside CancellingTask coroutine to get its @ref std::stop_token
		 */
		static constexpr detail::GetStopTokenTag get_stop_token{};

		/**
		 * co_await this field inside CancellingTask coroutine to get @ref std::stop_source with stop-state copied
		 */
		static constexpr detail::GetStopSourceTag get_stop_source{};

		/**
		* co_await this to get promise from withing the coroutine
		*/
		static constexpr detail::GetPromiseTag get_promise{};

		/**
		* co_await this to get handle, that can be used to connect network tasks
		* the type is implementation defined
		*/
		static constexpr detail::GetPromiseTag get_self = get_promise;
	}

	/**
	 * @brief Lazy coroutine with feature to request stop from task
	 */
	template<typename T, std::derived_from<Promise<T>> TaskPromise>
	class [[nodiscard]] CancellingTask {
	public:
		using promise_type = TaskPromise;
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

		CancellingTask() noexcept = default;
		CancellingTask(coroutine_handle coroutine) noexcept : coroutine_(coroutine) {}
		CancellingTask(const CancellingTask& other) = delete;
		CancellingTask(CancellingTask&& other) noexcept : coroutine_(std::exchange(other.coroutine_, nullptr)) {}
		~CancellingTask() noexcept {
			if (coroutine_) {
				coroutine_.destroy();
			}
		};

		CancellingTask& operator=(const CancellingTask& other) = delete;

		CancellingTask& operator=(const CancellingTask&& other) {
			if (std::addressof(other) == this) {
				return *this;
			}

			if (coroutine_) {
				coroutine_.destroy();
			}
			coroutine_ = std::exchange(other.coroutine_, nullptr);
			return *this;
		};

		auto operator co_await() const& noexcept {
			struct TaskAwaitable : Awaitable {
				decltype(auto) await_resume() const {
					return this->coroutine_.promise().result();
				}
			};

			return TaskAwaitable{ coroutine_ };
		}

		auto operator co_await() const&& noexcept {
			struct TaskAwaitable : Awaitable {
				decltype(auto) await_resume() {
					return std::move(this->coroutine_.promise()).result();
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

		coroutine_handle handle() const {
			return coroutine_;
		}

		bool done() const noexcept {
			return coroutine_.done();
		}

		bool resume() {
			if (!coroutine_.done()) {
				coroutine_.resume();
			}
			return !coroutine_.done();
		}

		/**
		 * Get the task @ref std::stop_source.
		 * You can use this to connect CancellingTask stop sources
		 * @return The stop_source with copy of stop-state of the task
		 */
		std::stop_source get_stop_source() {
			return coroutine_.promise().stop_source();
		}

		/**
		 * Set this coroutine @ref std::stop_source to provided.
		 * Used to connect stop_source of two CancellingTask objects
		 * @return Reference to this CancellingTask
		 */
		CancellingTask& update_stop_source(std::stop_source stop_source) & {
			coroutine_.promise().set_stop_source(std::move(stop_source));
			return *this;
		}

		/**
		 * Set this coroutine @ref std::stop_source to provided.
		 * Used to connect stop_source of two CancellingTask objects
		 * @return Reference to this CancellingTask
		 */
		CancellingTask&& update_stop_source(std::stop_source stop_source) && {
			coroutine_.promise().set_stop_source(std::move(stop_source));
			return std::move(*this);
		}

		/**
		 * Request task to stop
		 * @return True if @ref std::stop_source has a stop-state and invocation made a stop request.
		 *         Otherwise return false
		 */
		bool request_stop() noexcept {
			return coroutine_.promise().request_stop();
		}

	private:
		coroutine_handle coroutine_ = nullptr;
	};

	/**
	 * @brief Coroutine task associated with network transfers
	 * It will automatically connect task states when
	 * co_await NetworkTask (or derived from NetworkTask)
	 * inside another NetworkTask (or derived from NetworkTask)
	 * @note It cannot connect outside NetworkTask context.
	 *       You must manually call @see connect_with(...).
	 */
	template<typename T, std::derived_from<NetworkPromise<T>> TaskPromise>
	class [[nodiscard]] NetworkTask : public CancellingTask<T, TaskPromise> {
	public:

		using typename CancellingTask<T, TaskPromise>::promise_type;
		using coroutine_handle = std::coroutine_handle<promise_type>;

		struct Awaitable {
			Awaitable(coroutine_handle coroutine) noexcept : coroutine_(coroutine) {}

			bool await_ready() const noexcept {
				return !coroutine_ || coroutine_.done();
			}

			template<concepts::derived_from_template<NetworkPromise> U>
			std::coroutine_handle<> await_suspend(std::coroutine_handle<U> coroutine) noexcept {
				coroutine_.promise().connect_with(coroutine.promise());
				coroutine_.promise().continuation(coroutine);
				return coroutine_;
			}

			std::coroutine_handle<> await_suspend(std::coroutine_handle<> coroutine) noexcept {
				coroutine_.promise().continuation(coroutine);
				return coroutine_;
			}

			std::coroutine_handle<promise_type> coroutine_ = nullptr;
		};

		auto operator co_await() const& noexcept {
			struct TaskAwaitable : Awaitable {
				decltype(auto) await_resume() const {
					return this->coroutine_.promise().result();
				}
			};

			return TaskAwaitable{ this->handle() };
		}

		auto operator co_await() const&& noexcept {
			struct TaskAwaitable : Awaitable {
				decltype(auto) await_resume() {
					return std::move(this->coroutine_.promise()).result();
				}
			};

			return TaskAwaitable{ this->handle() };
		}

		/**
		 * @brief start the coroutine
		 * Just calls resume
		 * @return Reference to this NetworkTask
		 */
		NetworkTask& start() & {
			this->resume();
			return *this;
		}

		/**
		 * @brief start the coroutine
		 * Just calls resume
		 * @return R-value reference to this NetworkTask
		 */
		NetworkTask&& start() && {
			this->resume();
			return std::move(*this);
		}

		/**
		 * Connect two NetworkPromise.
		 * Then, if passed coroutine requests stop, sets information, it will be setted for this coroutine too.
		 * Used when you need to wrap NetworkTask object inside another coroutine, but want to use stop and info.
		 * @param other_promise The promise to get connected to
		 * @return Reference to this NetworkTask
		 */
		template<concepts::derived_from_template<NetworkPromise> U>
		NetworkTask& connect_with(U& other_promise) & {
			this->promise().connect_with(other_promise);
			return *this;
		}

		/**
		 * Connect two NetworkPromise.
		 * Then, if passed coroutine requests stop, sets information, it will be setted for this coroutine.
		 * Used when you need to wrap NetworkTask object inside another coroutine, but want to use stop and info.
		 * @param other_promise The promise to get connected to
		 * @return R-value reference to this NetworkTask
		 */
		template<concepts::derived_from_template<NetworkPromise> U>
		NetworkTask&& connect_with(U& other_promise) && {
			this->promise().connect_with(other_promise);
			return std::move(*this);
		}

		/**
		 * @return Already read bytes by the network request. Returns 0 if isn't started.
		 */
		long long read_bytes() const {
			return this->promise().read_bytes();
		}

		/**
		 * @note The value can always be 0 if the server isn't providing the 'Content-Length' field.
		 * @return Total bytes to read by the network request.
		 */
		long long total_bytes() const {
			return this->promise().total_bytes();
		}
	};

	template<typename T>
	CancellingTask<T> Promise<T>::get_return_object() noexcept {
		return CancellingTask<T>{ coroutine_handle::from_promise(*this) };
	}

	template<typename T>
	NetworkTask<T> NetworkPromise<T>::get_return_object() noexcept {
		return NetworkTask<T>{ coroutine_handle::from_promise(*this) };
	}
};