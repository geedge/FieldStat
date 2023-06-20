
# include(FetchContent)
# FetchContent_Declare(gtest
#   QUIET
#   URL https://github.com/google/googletest/archive/release-1.10.0.tar.gz
# )
# # configure build of googletest
# set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
# set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
# FetchContent_MakeAvailable(gtest)

cmake_minimum_required(VERSION 2.8)

# add_definitions(-std=c++11)

include_directories(${PROJECT_SOURCE_DIR}/test)
include_directories(${PROJECT_SOURCE_DIR}/src)
include_directories(${PROJECT_SOURCE_DIR}/src/utils)
include_directories(${PROJECT_SOURCE_DIR}/src/export)
include_directories(${PROJECT_SOURCE_DIR}/src/metrics)
include_directories(${PROJECT_SOURCE_DIR}/include)

# zlib, first install zlib from https://zlib.net/
SET (ZLIB_ROOT "/home/chenzizhan/zlib")
SET (ZLIB_INCLUDE_DIR "/home/chenzizhan/zlib/include/")
SET (ZLIB_LIBRARY "/home/chenzizhan/zlib/lib/libz.so.1.2.13")

FIND_PACKAGE (ZLIB REQUIRED)
MESSAGE (" zlib version major is " ${ZLIB_VERSION_MAJOR})
MESSAGE (" zlib version minor is " ${ZLIB_VERSION_MINOR})
MESSAGE (" zlib include is " ${ZLIB_INCLUDE_DIR})
MESSAGE (" zlib libraries are " ${ZLIB_LIBRARIES})

SET(TEST_UTILS_SRC
  utils.cpp
  )


add_executable(gtest_topk ${SRC} ${TEST_UTILS_SRC} gtest_topk.cpp)
target_link_libraries(gtest_topk gtest-static ${ZLIB_LIBRARIES})
add_executable(gtest_counter ${SRC} ${TEST_UTILS_SRC} gtest_counter.cpp)
target_link_libraries(gtest_counter gtest-static ${ZLIB_LIBRARIES})
add_executable(gtest_histogram ${SRC} ${TEST_UTILS_SRC} gtest_histogram.cpp)
target_link_libraries(gtest_histogram gtest-static ${ZLIB_LIBRARIES})
add_executable(gtest_cardinality ${SRC} ${TEST_UTILS_SRC} gtest_cardinality.cpp)
target_link_libraries(gtest_cardinality gtest-static ${ZLIB_LIBRARIES})