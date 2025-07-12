
#find_package(network REQUIRED)

include_directories(
	${ASYNCNET_INCLUDE_DIRS}
)

add_executable(AsyncnetExample1 "${CMAKE_CURRENT_LIST_DIR}/example1.cpp")

set_property(TARGET AsyncnetExample1 PROPERTY CXX_STANDARD 20)

target_link_libraries(AsyncnetExample1 PRIVATE asyncnet)