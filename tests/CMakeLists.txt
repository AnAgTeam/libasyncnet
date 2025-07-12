
include_directories(
	${ASYNCNET_INCLUDE_DIRS}
)

set(ASYNCNET_TESTS_SOURCES
	"catch_amalgamated.cpp"
	"net_types_test.cpp"
	"json_test.cpp"
	"networkRequestor_test.cpp"
	"queue_test.cpp"
	"UrlSession_test.cpp"
)
set(ASYNC_NETWORK_TESTS_HEADERS
	"catch_amalgamated.hpp"
)



add_executable(asyncnetTests ${ASYNCNET_TESTS_SOURCES} ${ASYNCNET_TESTS_HEADERS})

set_property(TARGET asyncnetTests PROPERTY CXX_STANDARD 23)

message(STATUS "asyncnet libraries: ${ASYNCNET_LIBRARIES}")

target_link_libraries(asyncnetTests PRIVATE 
	${ASYNCNET_LIBRARIES}
)