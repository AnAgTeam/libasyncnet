
set(ASYNCNET_TESTS_SOURCES
	"catch_amalgamated.cpp"
	"net_types_test.cpp"
	"json_test.cpp"
	"requestor_test.cpp"
	"queue_test.cpp"
	"session_test.cpp"
	"request_test.cpp"
)
set(ASYNC_NETWORK_TESTS_HEADERS
	"catch_amalgamated.hpp"
)

if (ASYNCNET_BUILD_TESTS_NETWORK)
	add_definitions(-DASYNCNET_ENABLE_TESTS_NETWORK)
endif()

add_executable(asyncnetTests ${ASYNCNET_TESTS_SOURCES} ${ASYNCNET_TESTS_HEADERS})

set_property(TARGET asyncnetTests PROPERTY CXX_STANDARD 23)

target_link_libraries(asyncnetTests PRIVATE 
	asyncnet
)