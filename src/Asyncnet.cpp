#include <asyncnet/Asyncnet.hpp>
#include <curlpp/cURLpp.hpp>

namespace asyncnet {
	void init() {
		curlpp::initialize();
	}

	void deinit() {
		curlpp::terminate();
	}
}