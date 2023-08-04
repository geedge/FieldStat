cmake_minimum_required(VERSION 2.8.8)

message("-- CMAKE_C_COMPILER: ${CMAKE_C_COMPILER_ID}")

set(lib_name fieldstat4)

#the path where you install the cppcheck


project (${lib_name})

# to fix issue: cannot find pthresh
# set(CMAKE_THREAD_LIBS_INIT "-lpthread")
# set(CMAKE_HAVE_THREADS_LIBRARY 1)
# set(CMAKE_USE_WIN32_THREADS_INIT 0)
# set(CMAKE_USE_PTHREADS_INIT 1)
# set(THREADS_PREFER_PTHREAD_FLAG ON)

set(CMAKE_MODULE_PATH ${PROJECT_SOURCE_DIR}/cmake)
include(Version)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_MACOSX_RPATH 0)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -fPIC -Wall -lm -lz --std=gnu11")
set(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} -Wall)

set(C_INCLUDE_PATH ${C_INCLUDE_PATH} /opt/MESA/include)
set(CPLUS_INCLUDE_PATH ${CPLUS_INCLUDE_PATH} /opt/MESA/include)
set (LIBRARY_PATH ${LIBRARY_PATH} /opt/MESA/lib)

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
	set(CMAKE_INSTALL_PREFIX "/opt/MESA" CACHE PATH "default install path" FORCE)
endif()

set(CPP_BIN_PATH "/home/chenzizhan/cppcheck/cfg")
find_program(CMAKE_CXX_CPPCHECK NAMES cppcheck HINTS ${CPP_BIN_PATH})
if (CMAKE_CXX_CPPCHECK)
    list(
        APPEND CMAKE_CXX_CPPCHECK 
            "--enable=all"
            "--suppress=missingIncludeSystem"
            "--error-exitcode=1"
            "--suppress=unreachableCode"
            "--suppress=unusedFunction"
            "--suppress=missingInclude"
            "--suppress=uselessAssignmentPtrArg"
            "--suppress=unmatchedSuppression"
            "--suppress=internalAstError"
            "--suppress=unmatchedSuppression"
            "--suppress=unreadVariable"
            "--suppress=memleakOnRealloc"
            "--suppress=redundantAssignment"
            "--suppress=constParameter"
            "--suppress=unsignedLessThanZero"
            "--suppress=variableScope"
            "--suppress=cstyleCast"
            "--suppress=objectIndex"
            "--suppress=shadowVariable"
            "--suppress=nullPointerRedundantCheck"
            "--suppress=ctunullpointer"
            "--suppress=shadowFunction"
            "--suppress=shiftTooManyBitsSigned"
            "--suppress=preprocessorErrorDirective"
            "--inline-suppr"
            "--suppress=*:${PROJECT_SOURCE_DIR}/vendors/*"
            "--suppress=*:${PROJECT_SOURCE_DIR}/test/utils.hpp"
            "--suppress=*:${PROJECT_SOURCE_DIR}/test/unit_test_serialize.cpp"
    )
    set(CMAKE_C_CPPCHECK ${CMAKE_CXX_CPPCHECK})
else()
  message(FATAL_ERROR "Could not find the program cppcheck.")
endif()

#for ASAN
set(ASAN_OPTION "OFF" CACHE STRING " set asan type chosen by the user, using OFF as default")
set_property(CACHE ASAN_OPTION  PROPERTY STRINGS OFF ADDRESS THREAD)
message(STATUS "ASAN_OPTION='${ASAN_OPTION}'")

if(ASAN_OPTION MATCHES "ADDRESS")
    set(CMAKE_C_FLAGS "${CMAKADDRESS} -g -DCMAKE_BUILD_TYPE=Debug -fsanitize=address -fno-omit-frame-pointer")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -DCMAKE_BUILD_TYPE=Debug -fsanitize=address -fno-omit-frame-pointer")
	set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -lasan")
	set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -lasan")
elseif(ASAN_OPTION MATCHES "THREAD")
	set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -DCMAKE_BUILD_TYPE=Debug -fsanitize=thread -fno-omit-frame-pointer")
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -DCMAKE_BUILD_TYPE=Debug -fsanitize=thread -fno-omit-frame-pointer")
	set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -lasan")
	set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -lasan")
endif()
# end of for ASAN


include_directories(${PROJECT_SOURCE_DIR}/vendors)
include_directories(${PROJECT_SOURCE_DIR}/src)
include_directories(${PROJECT_SOURCE_DIR}/include/fieldstat)
include_directories(${PROJECT_SOURCE_DIR}/src/metrics)
include_directories(${PROJECT_SOURCE_DIR}/src/tags)


file(GLOB SRC
    "src/*.c"
    "src/metrics/*.c"
    "src/tags/*.c"
    "src/exporter/*.c"
    "vendors/cjson/*.c"
    "vendors/hdr/*.c"
    "vendors/minheap/*.c"
    "vendors/mpack/*.c"
    "vendors/xxhash/*.c"
    "vendors/base64/*.c"
)

add_subdirectory(test/deps)

add_subdirectory(test)
enable_testing()
add_subdirectory(ctest)



# Shared Library Output
add_library(${lib_name}_shared SHARED ${SRC})
set_target_properties(${lib_name}_shared PROPERTIES LINK_FLAGS
    "-Wl,--version-script=${PROJECT_SOURCE_DIR}/src/version.map")
if(DEFINED MESA_SHARED_INSTALL_DIR)
	set_target_properties(${lib_name}_shared PROPERTIES OUTPUT_NAME ${lib_name} LIBRARY_OUTPUT_DIRECTORY ${MESA_SHARED_INSTALL_DIR})
else()
	set_target_properties(${lib_name}_shared PROPERTIES OUTPUT_NAME ${lib_name})
endif()

# static Library Output
add_library(${lib_name}_static STATIC ${SRC})
set_target_properties(${lib_name}_static PROPERTIES OUTPUT_NAME ${lib_name})

install(TARGETS ${lib_name}_shared LIBRARY DESTINATION ${CMAKE_INSTALL_PREFIX}/lib COMPONENT LIBRARIES)
install(TARGETS ${lib_name}_static LIBRARY ARCHIVE DESTINATION ${CMAKE_INSTALL_PREFIX}/lib COMPONENT LIBRARIES)
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_PREFIX}/include COMPONENT HEADER)

include(Package)