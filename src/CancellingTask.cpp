#include <asyncnet/CancellingTask.hpp>

namespace asyncnet {
	namespace detail {
		CancellingTask<void> Promise<void>::get_return_object() noexcept {
			return CancellingTask<void>{ coroutine_handle::from_promise(*this) };
		}
	}
}