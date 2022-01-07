cmake_minimum_required(VERSION 3.1)

project(kal VERSION 0.4.2)

set(CMAKE_BUILD_TYPE Release)

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

add_compile_options(-Wall)
add_compile_options(-Wextra)
add_compile_options(-Wsign-compare)
#http://gcc.gnu.org/wiki/Visibility
add_compile_options(-fvisibility=hidden)
add_definitions(-D_GNU_SOURCE)
add_definitions(-DHAVE_DITHERING=1)

if(MINGW)
# Fix printf %zu
ADD_DEFINITIONS(-D__USE_MINGW_ANSI_STDIO) 
endif()

include_directories(${PROJECT_SOURCE_DIR}/src)
link_directories(${PROJECT_SOURCE_DIR}/lib)

add_executable(kal ${SOURCE_FILES})

target_link_libraries(kal -lrtlsdr.dll -lfftw3-3 -s)
