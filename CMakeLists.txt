cmake_minimum_required (VERSION 3.18)

# ---- PREPARE

option(ASYNCNET_INCLUDE_VCPKG "include vcpkg cmake" OFF)
option(ASYNCNET_BUILD_TESTS "build tests" OFF)
option(ASYNCNET_BUILD_TESTS_NETWORK "build tests with network request. Works only with ASYNCNET_BUILD_TESTS" OFF)
option(ASYNCNET_BUILD_EXAMPLES "build examples" OFF)

# without this vcpkg won't try to install dependencies
if (CMAKE_TOOLCHAIN_FILE MATCHES "vcpkg.cmake")
	message(STATUS "Found vcpkg")
	include(${CMAKE_TOOLCHAIN_FILE})
endif()

project(asyncnet VERSION 2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CXX_STANDARD_REQUIRED ON)

# ---- FIND DEPENDENCIES
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

find_package(Boost REQUIRED COMPONENTS json)
find_package(CURLpp REQUIRED)
find_package(libcoro REQUIRED)

# ---- ADD LIBRARY

file(GLOB_RECURSE ASYNCNET_HEADER_FILES "${CMAKE_CURRENT_LIST_DIR}/include/*.hpp")
file(GLOB_RECURSE ASYNCNET_SOURCE_FILES "${CMAKE_CURRENT_LIST_DIR}/src/*.cpp")

add_library(asyncnet STATIC ${ASYNCNET_HEADER_FILES} ${ASYNCNET_SOURCE_FILES})

target_include_directories(asyncnet PUBLIC
	$<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>
	$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_include_directories(asyncnet PUBLIC
	$<BUILD_INTERFACE:${Boost_INCLUDE_DIRS}>
	$<BUILD_INTERFACE:${CURLPP_INCLUDE_DIRS}>
	$<BUILD_INTERFACE:${LIBCORO_INCLUDE_DIRS}>
)

target_link_libraries(asyncnet PUBLIC
	$<BUILD_INTERFACE:${Boost_LIBRARIES}>
	$<BUILD_INTERFACE:${CURLPP_LIBRARIES}>
	$<BUILD_INTERFACE:${LIBCORO_LIBRARIES}>
)

if (ASYNCNET_BUILD_TESTS)
	enable_testing()
	add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/tests")
endif()

if (ASYNCNET_BUILD_EXAMPLES)
	add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/examples")
endif()

# ---- CONFIGURE PACKAGE

include(CMakePackageConfigHelpers)

configure_package_config_file(
	"${CMAKE_CURRENT_LIST_DIR}/asyncnet-config.cmake.in"
	"${CMAKE_CURRENT_BINARY_DIR}/asyncnet-config.cmake"
	INSTALL_DESTINATION
		"{CMAKE_INSTALL_LIBDIR}/cmake/asyncnet"
	NO_SET_AND_CHECK_MACRO
	NO_CHECK_REQUIRED_COMPONENTS_MACRO
)

configure_file("asyncnet.pc.in" "asyncnet.pc" @ONLY)

# always prefix with lib*
set_target_properties(asyncnet PROPERTIES PREFIX "lib")

install(DIRECTORY "include/" DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/asyncnet.pc DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)

install(TARGETS libcoro curlpp_static)

install(TARGETS asyncnet
	EXPORT asyncnet-targets
	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
	ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
	LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
	INCLUDES DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
)

install(EXPORT asyncnet-targets
	NAMESPACE asyncnet::
	DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/asyncnet"
	EXPORT_LINK_INTERFACE_LIBRARIES
)