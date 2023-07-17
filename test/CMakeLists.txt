
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

set(DEBUG_FLAGS "-O0")

include_directories(${PROJECT_SOURCE_DIR}/test)
include_directories(${PROJECT_SOURCE_DIR}/src)
include_directories(${PROJECT_SOURCE_DIR}/src/tags)
include_directories(${PROJECT_SOURCE_DIR}/src/metrics)
include_directories(${PROJECT_SOURCE_DIR}/include/fieldstat)

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

function (add_unit_test file_name)
    add_executable(${file_name} ${SRC} ${TEST_UTILS_SRC} ${file_name}.cpp)
    target_link_libraries(${file_name} gtest-static ${ZLIB_LIBRARIES})
    set_property(TARGET ${file_name} PROPERTY CXX_STANDARD 17)
    # add_test(NAME ${file_name} COMMAND ${file_name})
endfunction()

# add_unit_test(test_metric_counter)
# add_unit_test(test_metric_histogram)
# add_unit_test(test_metric_hll)
# add_unit_test(unit_test_merge)
# add_unit_test(unit_test_serialize)
# add_unit_test(test_exporter_json)
add_unit_test(fuzz_test)