# asyncnet
<b>asyncnet</b> is an asyncronous http client library. Written upon libcurl ([curlpp](https://github.com/jpbarrette/curlpp)) and [libcoro](https://github.com/jbaldwin/libcoro)
## Usage
Create basic request
```cpp
#include <asyncnet/Request.hpp>

coro::task<void> amain() {
  // make request with the url
  asyncnet::Request request("http://example.com");

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
  asyncnet::Request request1("http://example.com");
  asyncnet::Request request2("http://example.com");

  // AsyncSession automatically enables cookie engine and cookie sharing across all the requests
  // The session creates background thread which will perform requests
  AsyncSession session;

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