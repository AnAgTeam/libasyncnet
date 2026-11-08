# asyncnet
<b>asyncnet</b> is an asyncronous http client library. Written upon libcurl ([curlpp](https://github.com/jpbarrette/curlpp)) and [libcoro](https://github.com/jbaldwin/libcoro)

## Overview
- [Create basic request](#create-basic-request)
- [Create API methods](#create-api-methods)
- [Make simultaneous requests](#make-simultaneous-requests)
- [Custom task with cancel support](#create-custom-task-with-cancel-support)

## Usage
### Create basic request
```cpp
#include <asyncnet/Request.hpp>

coro::task<void> amain() {
  // make request with the url
  asyncnet::GetRequest request("http://example.com");

  // Set URL parameters which will be concatenated with request's url
  request.set_url_parameters({
    { "param1", "value" }
  });

  // You can set any option of the `curlpp`
  request.set_option<curlpp::options::ConnectTimeout>(10);

  // Activate cookie engine
  request.set_cookie_file(asyncnet::Request::cookie_memory);
}
```
Perform 2 asyncronous requests
```cpp
#include <asyncnet/AsyncSession.hpp>
#include <asyncnet/Exceptions.hpp>
#include <coro/when_all.hpp>

coro::task<void> amain() {
  // AsyncSession automatically enables cookie engine and cookie sharing across all the requests
  // The session creates background thread which will perform requests
  asyncnet::AsyncSession session;

  asyncnet::GetRequest request1 = session.make_request<asyncnet::GetRequest>("http://example.com");
  asyncnet::GetRequest request2 = session.make_request<asyncnet::GetRequest>("http://example.com");

  // Await all the requests asyncronously
  auto [request1_task, request2_task] = co_await coro::when_all(
    session.perform_request(request1),
    session.perform_request(request2)
  );

  // Get the responses. The exception will be retrown when you are getting task return value
  try {
    asyncnet::Response response1 = std::move(request1_task.return_value());
    asyncnet::Response response2 = std::move(request2_task.return_value());
    
    // Get response text and status code
    long status_code = response1.get_status_code();
    std::string response1_text = response1.get_text();
  } catch (const NetworkRuntimeError& e) {
    // process exception
  }
  catch (...) {

  }
}
```

### Create API methods
You can create methods performing some API methods, and then get cancel it from task if needed.
```cpp
#include <asyncnet/AsyncSession.hpp>
#include <asyncnet/WhenAll.hpp>
#include <coro/sync_wait.hpp>

asyncnet::NetworkTask<int> my_api_request(std::shared_ptr<asyncnet::AsyncSession> session, int index) {
	// make request
	auto request = session->make_request<asyncnet::GetRequest>("https://myapi.com/get");
	request.set_url_parameters({
		{ "param", std::to_string(index) }
	});
	// perform it
	asyncnet::Response response = co_await session->perform_request(request);
	// return API value
	co_return std::stoi(std::move(response).get_text());
}

asyncnet::NetworkTask<int> my_api_post_request(std::shared_ptr<asyncnet::AsyncSession> session, int index) {
	// make requests
	auto request = session->make_request<asyncnet::PostRequest>("https://myapi.com/post", "customdata");
	request.set_url_parameters({
		{ "param", std::to_string(index) }
	});
	auto info_request = session->make_request<asyncnet::GetRequest>("https://myapi.com/post_info");
	// perform requests simultaneously with stop support
	auto [post_task, info_task] = co_await asyncnet::when_all(
		co_await asyncnet::awaitables::get_stop_source,
		session->perform_request(request),
		session->perform_request(info_request)
	);
	// return API value
	co_return std::stoi(std::move(post_task.return_value()).get_text());
}

int main() {
	auto session = std::make_shared<asyncnet::AsyncSession>();

	auto api_task = my_api_request(session, 10);
	auto cancel_api_task = my_api_post_request(session, 10);

	// make request
	auto api_value = coro::sync_wait(api_task);
	// or just cancel it
	cancel_api_task.request_stop();

	std::cout << "Got value: " << api_value << '\n';
}
```
### Make simultaneous requests
Multiple requests can be co_await'ed requests in parallel and either:
* get individual tasks via `when_all`. Exception is rethrown when calling `return_value()` 
* get values via `gather`. It waits for every child, then rethrows the first
  exception in argument order from `co_await`, even if the results are discarded.
  Results are returned only if all children succeeded. Use `when_all` to inspect
  individual failures and successful results separately.

`gather` is available from `<asyncnet/Gather.hpp>` without libcoro. It owns its
inputs and returned results keep the child coroutine frames alive. Value results
are exposed by reference into those frames; explicit lvalue reference results
keep their original identity, and `void` results use `std::monostate`. Keep the
result object alive while using references into it. Borrowed inputs, coroutine
receivers and externally referenced results must outlive their use.

Built-in `CancellingTask` and `NetworkTask` inputs with their default promises
notify the group from `final_suspend` through a generic completion callback.
The promise stores only a context pointer and a function pointer; it does not
know about groups. The callback returns a coroutine handle for symmetric
transfer rather than resuming the continuation itself. Constructing a group needs
one allocation for stable group storage, with no extra coroutine frame per input
(the original task frames are already allocated). Other awaitables, including
custom task/promise types, use coroutine adapters to preserve their await protocol.
Both paths can be mixed in one group.

Inside an asyncnet task, `gather` automatically shares the parent's stop state
with cancellable children. Network children also share a network parent's progress
counters; these counters are shared observations, not a sum of parallel transfers.
The explicit `gather(stop_source, ...)` overload overrides the inherited stop state
and also works from foreign coroutines. Cancellation is cooperative: the group
still waits for every child, including awaitables without cancellation support.
Asyncnet task inputs must be unstarted or already completed, never in flight.
Await the group once; do not move or destroy an active group or its awaiting
coroutine.

```cpp
#include <asyncnet/AsyncSession.hpp>
#include <asyncnet/WhenAll.hpp>
#include <coro/sync_wait.hpp>

asyncnet::NetworkTask<void> my_api_post_request(std::shared_ptr<asyncnet::AsyncSession> session, int index) {
	// make requests
	auto request = session->make_request<asyncnet::PostRequest>("https://myapi.com/post", "customdata");
	auto info_request = session->make_request<asyncnet::GetRequest>("https://myapi.com/post_info");

	// perform requests simultaneously via when_all with stop support
	// co_await'ing asyncnet::awaitables::get_stop_source returns stop_source
	// of current task promise
	auto [post_task, info_task] = co_await asyncnet::when_all(
		co_await asyncnet::awaitables::get_stop_source,
		session->perform_request(request),
		session->perform_request(info_request)
	);

	// then get values and catch errors
	try {
		asyncnet::Response resp1 = std::move(post_task.return_value());
		asyncnet::Response resp2 = std::move(info_task.return_value());
	}
	catch (...) {

	}

	// or get values in-place
	try {
		auto [response1, response2] = co_await asyncnet::gather(
			session->perform_request(request),
			session->perform_request(info_request)
		);

		std::cout << response1.get_text() << std::endl;
	}
	catch (...) {

	}
}

int main() {
	auto session = std::make_shared<asyncnet::AsyncSession>();

	// make request
	coro::sync_wait(my_api_post_request(session, 123));
}
```
### Create custom task with cancel support
Minimal setup for integrating custom tasks and promises
```cpp
#include <asyncnet/CancellingTask.hpp>

// forward definition for DerivedPromise<T>::get_return_object()
template<typename T>
struct DerivedTask;

// custom promise derived from NetworkPromise
template<typename T>
struct DerivedPromise : NetworkPromise<T> {
	using coroutine_handle = std::coroutine_handle<DerivedPromise>;

	DerivedTask<T> get_return_object();

	int awesome_field;
};

// custom task derived from NetworkTask.
// Note to forward NetworkTask promise type and typedef promise_type, coroutine_handle
template<typename T>
struct DerivedTask : NetworkTask<T, DerivedPromise<T>> {
	using promise_type = DerivedPromise<T>;
	using coroutine_handle = std::coroutine_handle<promise_type>;
};

// return object from coroutine function
template<typename T>
DerivedTask<T> DerivedPromise<T>::get_return_object() {
	return DerivedTask<T>{ coroutine_handle::from_promise(*this) };
}

DerivedTask<void> derived_coroutine() {
	// ...
	// co_await request;
};
```

## Task-only CMake component

`asyncnet::task` provides `CancellingTask`, `NetworkTask`, cancellation, and
progress using only the C++20 standard library. It does not require curl,
Boost, libcoro, or any initialized dependency submodules.

For an embedded checkout:

```cmake
set(ASYNCNET_BUILD_CLIENT OFF)
add_subdirectory(libasyncnet)
target_link_libraries(my_target PRIVATE asyncnet::task)
```

To install only the task component:

```sh
cmake -S . -B build-task -DASYNCNET_BUILD_CLIENT=OFF -DCMAKE_INSTALL_PREFIX=/your/prefix
cmake --install build-task
```

An installed consumer uses:

```cmake
find_package(asyncnet CONFIG REQUIRED COMPONENTS task)
target_link_libraries(my_target PRIVATE asyncnet::task)
```

This component also works with a full installation without discovering the
HTTP client's dependencies. `ASYNCNET_BUILD_CLIENT` defaults to `ON` for existing
client builds; its vcpkg dependencies are selected through the `client` feature.
Initialize the asyncnet submodule without `--recursive` when only tasks are needed.
`ASYNCNET_BUILD_TESTS=ON` runs the dependency-free task/progress/gather suite even
with the client disabled. `Gather.hpp` is included in task-only installations.
The `when_all` adapter in `WhenAll.hpp` still requires libcoro and is not part of
the task-only installed headers. Existing full-client users can also access
`gather` through `WhenAll.hpp`.
