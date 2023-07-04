cmake_minimum_required(VERSION 2.8.8)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3 -g -fPIC -Wall -lm -lz")

set(PROJECT_SOURCE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../..)
#print 
message(STATUS "PROJECT_SOURCE_DIR: ${PROJECT_SOURCE_DIR}")

include_directories(${PROJECT_SOURCE_DIR}/vendors)
include_directories(${PROJECT_SOURCE_DIR}/src)
include_directories(${PROJECT_SOURCE_DIR}/include/fieldstat)
include_directories(${PROJECT_SOURCE_DIR}/src/metrics)
include_directories(${PROJECT_SOURCE_DIR}/src/utils)


file(GLOB SRC
    "${PROJECT_SOURCE_DIR}/src/*.c"
    "${PROJECT_SOURCE_DIR}/src/metrics/*.c"
    "${PROJECT_SOURCE_DIR}/src/utils/*.c"
    "${PROJECT_SOURCE_DIR}/src/exporter/*.c"
    "${PROJECT_SOURCE_DIR}/vendors/cjson/*.c"
    "${PROJECT_SOURCE_DIR}/vendors/hdr/*.c"
    "${PROJECT_SOURCE_DIR}/vendors/minheap/*.c"
    "${PROJECT_SOURCE_DIR}/vendors/mpack/*.c"
    "${PROJECT_SOURCE_DIR}/vendors/xxhash/*.c"
    "${PROJECT_SOURCE_DIR}/vendors/base64/*.c"
)

add_executable(myapp ${SRC} main.c)