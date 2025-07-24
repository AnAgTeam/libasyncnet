# TODO: add CURLPP_DEFINITIONS, CURLPP_EXECUTABLE, CURLPP_ROOT_DIR

set(CURLPP_BUILD_SHARED_LIBS OFF)

add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/curlpp")

set(CURLPP_LIBRARIES curlpp::curlpp_static)
set(CURLPP_INCLUDE_DIRS "${CMAKE_CURRENT_SOURCE_DIR}/curlpp/include")
set(CURLPP_FOUND ON)


# include(FetchContent)
# FetchContent_Declare(CURLPP GIT_REPOSITORY https://github.com/AnAgTeam/curlpp)
# FetchContent_MakeAvailable(CURLPP)

message(STATUS "Found curlpp libraries: ${CURLPP_LIBRARIES}")
message(STATUS "Found curlpp includes: ${CURLPP_INCLUDE_DIRS}")