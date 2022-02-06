cmake_minimum_required(VERSION 3.1)

project(kal VERSION 0.4.2)

#select the release build type by default to get optimization flags
if(NOT CMAKE_BUILD_TYPE)
   set(CMAKE_BUILD_TYPE "Release")
   message(STATUS "Build type not specified: defaulting to release.")
endif(NOT CMAKE_BUILD_TYPE)
set(CMAKE_BUILD_TYPE ${CMAKE_BUILD_TYPE} CACHE STRING "")

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

find_package(PkgConfig)

find_package(LibRTLSDR)
if (NOT LIBRTLSDR_FOUND)
    message(FATAL_ERROR "please install librtlsdr")
endif ()
message(STATUS "LIBRTLSDR_INCLUDE_DIR: ${LIBRTLSDR_INCLUDE_DIR}")
message(STATUS "LIBRTLSDR_LIBRARIES: ${LIBRTLSDR_LIBRARIES}")

if(WIN32 AND NOT MINGW)
    set(THREADS_USE_PTHREADS_WIN32 true)
endif()

find_package(Threads)
if(NOT THREADS_FOUND)
    message(FATAL_ERROR "pthreads(-win32) required to compile kal")
endif()
message(STATUS "THREADS_PTHREADS_INCLUDE_DIR: ${THREADS_PTHREADS_INCLUDE_DIR}")
message(STATUS "CMAKE_THREAD_LIBS_INIT: ${CMAKE_THREAD_LIBS_INIT}")

find_package(FFTW3)
if (NOT FFTW3_FOUND)
    message(FATAL_ERROR "please install FFTW3")
endif ()
message(STATUS "FFTW3_INCLUDE_DIR: ${FFTW3_INCLUDE_DIRS}")
message(STATUS "FFTW3_LIBRARIES: ${FFTW3_LIBRARIES}")

set(SOURCE_FILES 
   src/arfcn_freq.cc
   src/c0_detect.cc
   src/circular_buffer.cc
   src/fcch_detector.cc
   src/kal.cc
   src/offset.cc
   src/util.cc
   src/usrp_source.cc
)

add_executable(kal ${SOURCE_FILES})

target_compile_options(kal PRIVATE -Wall -Wextra -Wsign-compare -fvisibility=hidden -s)
target_compile_definitions(kal PRIVATE _GNU_SOURCE=1 HAVE_DITHERING=1 HAVE_GET_TUNER_GAIN=1)

if(MINGW)
    # Fix printf %zu
    ADD_DEFINITIONS(-D__USE_MINGW_ANSI_STDIO) 
endif()

target_include_directories(kal PRIVATE
    ${PROJECT_SOURCE_DIR}/src
    ${LIBRTLSDR_INCLUDE_DIR}
    ${FFTW_INCLUDE_DIRS}
    ${THREADS_PTHREADS_INCLUDE_DIR}
)

target_link_libraries(kal PRIVATE
    ${LIBRTLSDR_LIBRARIES}
    ${FFTW3_LIBRARIES} 
    ${CMAKE_THREAD_LIBS_INIT}
    -s
)

########################################################################
# Install built library files & utilities
########################################################################
install(TARGETS kal)

########################################################################
# Create uninstall target
########################################################################
configure_file(
    ${PROJECT_SOURCE_DIR}/cmake/cmake_uninstall.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake
@ONLY)

add_custom_target(uninstall
    ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake
)
