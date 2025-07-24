#include <asyncnet/NetworkTask.hpp>

namespace asyncnet {
	namespace detail {

		std::suspend_always Promise::initial_suspend() noexcept {
			return {};
		}

		Promise::FinalAwaitable Promise::final_suspend() noexcept {
			return {};
		}

		NetworkTask Promise::get_return_object() noexcept {
			return NetworkTask{ std::coroutine_handle<detail::Promise>::from_promise(*this) };
		}

		void Promise::unhandled_exception() noexcept {
			storage_.emplace<std::exception_ptr>(std::current_exception());
		}

		void Promise::continuation(std::coroutine_handle<> coroutine) noexcept {
			continuation_ = coroutine;
		}

		void Promise::return_value(Response&& response) {
			storage_.emplace<Response>(std::move(response));
		}

		Response& Promise::result() & {
			return const_cast<Response&>(static_cast<const Promise*>(this)->result());
		}

		const Response& Promise::result() const& {
			if (std::holds_alternative<Response>(storage_)) {
				return static_cast<const Response&>(std::get<Response>(storage_));
			}
			else if (std::holds_alternative<std::exception_ptr>(storage_)) {
				std::rethrow_exception(std::get<std::exception_ptr>(storage_));
			}
			else {
				throw std::runtime_error("Promise value is unset");
			}
		}

		Response&& Promise::result() && {
			return static_cast<Response&&>(static_cast<Promise*>(this)->result());
		}

		bool Promise::request_stop() noexcept {
			return stop_source_.request_stop();
		}
	}

	NetworkTask::Awaitable::Awaitable(coroutine_handle coroutine) noexcept : coroutine_(coroutine) {

	}

	bool NetworkTask::Awaitable::await_ready() const noexcept {
		return !coroutine_ || coroutine_.done();
	}

	std::coroutine_handle<> NetworkTask::Awaitable::await_suspend(std::coroutine_handle<> coroutine) noexcept {
		coroutine_.promise().continuation(coroutine);
		return coroutine_;
	}

	NetworkTask::NetworkTask(coroutine_handle coroutine) noexcept : coroutine_(coroutine) {

	}

	NetworkTask::NetworkTask(NetworkTask&& other) noexcept : coroutine_(std::exchange(other.coroutine_, nullptr)) {

	}

	NetworkTask::~NetworkTask() noexcept {
		if (coroutine_) {
			coroutine_.destroy();
		}
	}

	NetworkTask::promise_type& NetworkTask::promise() & noexcept {
		return coroutine_.promise();
	}

	const NetworkTask::promise_type& NetworkTask::promise() const& noexcept {
		return coroutine_.promise();
	}

	NetworkTask::promise_type&& NetworkTask::promise() && noexcept {
		return std::move(coroutine_.promise());
	}

	NetworkTask::coroutine_handle NetworkTask::handle() {
		return coroutine_;
	}

	bool NetworkTask::request_stop() noexcept {
		return coroutine_.promise().request_stop();
	}
}