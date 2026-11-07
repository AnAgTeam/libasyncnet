cmake_minimum_required (VERSION 3.18)

# ---- PREPARE

option(ASYNCNET_BUILD_CLIENT "Build the HTTP client and its dependencies" ON)
option(ASYNCNET_BUILD_TESTS "build tests, Default=OFF" OFF)
option(ASYNCNET_BUILD_TESTS_NETWORK "build tests with network request. Works only with ASYNCNET_BUILD_TESTS, Default=ON" ON)
option(ASYNCNET_BUILD_EXAMPLES "build examples, Default=OFF" OFF)

# Select dependencies before the standalone vcpkg toolchain is loaded.
if (ASYNCNET_BUILD_CLIENT AND CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    list(APPEND VCPKG_MANIFEST_FEATURES "client")
endif()

# without this vcpkg won't try to install dependencies
if (CMAKE_TOOLCHAIN_FILE MATCHES "vcpkg.cmake")
    message(STATUS "Found vcpkg")
    include(${CMAKE_TOOLCHAIN_FILE})
endif()

project(asyncnet VERSION 2.0 LANGUAGES CXX)
include(GNUInstallDirs)

# Tasks require only the standard library; no runtime or HTTP backend is linked.
add_library(asyncnet-task INTERFACE)
add_library(asyncnet::task ALIAS asyncnet-task)
set_target_properties(asyncnet-task PROPERTIES EXPORT_NAME task)
target_compile_features(asyncnet-task INTERFACE cxx_std_20)
target_include_directories(asyncnet-task INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
if (APPLE AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 17.0)
    target_compile_options(asyncnet-task INTERFACE -fexperimental-library)
endif()

if (ASYNCNET_BUILD_CLIENT)
    # ---- FIND DEPENDENCIES
    list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/cmake")

    find_package(Boost REQUIRED COMPONENTS json)
    find_package(CURLpp REQUIRED)
    find_package(libcoro REQUIRED)

    # ---- ADD LIBRARY

    file(GLOB_RECURSE ASYNCNET_HEADER_FILES "${CMAKE_CURRENT_LIST_DIR}/include/*.hpp")
    file(GLOB_RECURSE ASYNCNET_SOURCE_FILES "${CMAKE_CURRENT_LIST_DIR}/src/*.cpp")

    add_library(asyncnet STATIC ${ASYNCNET_HEADER_FILES} ${ASYNCNET_SOURCE_FILES})

    set_property(TARGET asyncnet PROPERTY CXX_STANDARD 20)
    set_property(TARGET asyncnet PROPERTY CXX_STANDARD_REQUIRED ON)

    target_include_directories(asyncnet PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_LIST_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
    )

    # Boost::json is a find_package'd imported target a consumer recreates via
    # find_dependency (see asyncnet-config.cmake.in), so it belongs on the install
    # interface too. curlpp / libcoro are vendored (Find modules add_subdirectory
    # them), not installed, so they stay build-only.
    target_link_libraries(asyncnet PUBLIC
        asyncnet-task
        Boost::json
        $<BUILD_INTERFACE:${CURLPP_LIBRARIES}>
        $<BUILD_INTERFACE:${LIBCORO_LIBRARIES}>
    )

endif() # ASYNCNET_BUILD_CLIENT

if (ASYNCNET_BUILD_TESTS)
    enable_testing()
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/tests")
endif()

if (ASYNCNET_BUILD_EXAMPLES AND ASYNCNET_BUILD_CLIENT)
    add_subdirectory("${CMAKE_CURRENT_LIST_DIR}/examples")
endif()

# ---- CONFIGURE PACKAGE

include(CMakePackageConfigHelpers)

configure_package_config_file(
    "${CMAKE_CURRENT_LIST_DIR}/asyncnet-config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/asyncnet-config.cmake"
    INSTALL_DESTINATION
        "${CMAKE_INSTALL_LIBDIR}/cmake/asyncnet"
    NO_SET_AND_CHECK_MACRO
)

write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/asyncnet-config-version.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion
)

if (ASYNCNET_BUILD_CLIENT)
    configure_file("asyncnet.pc.in" "asyncnet.pc" @ONLY)

    # always prefix with lib*
    set_target_properties(asyncnet PROPERTIES PREFIX "lib")

    install(DIRECTORY "include/" DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    install(FILES ${CMAKE_CURRENT_BINARY_DIR}/asyncnet.pc DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)

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

endif() # ASYNCNET_BUILD_CLIENT

install(FILES include/asyncnet/CancellingTask.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/asyncnet)
install(FILES include/asyncnet/utility/Concepts.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/asyncnet/utility)
install(TARGETS asyncnet-task EXPORT asyncnet-task-targets)
install(EXPORT asyncnet-task-targets NAMESPACE asyncnet::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/asyncnet)

# The package config + version file themselves — without this find_package(asyncnet)
# finds no config and imports nothing.
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/asyncnet-config.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/asyncnet-config-version.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/asyncnet"
)
