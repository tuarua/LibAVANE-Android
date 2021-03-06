# Sets the minimum version of CMake required to build the native
# library. You should either keep the default value or only pass a
# value of 3.4.0 or lower.

cmake_minimum_required(VERSION 3.4.1)

set(CMAKE_VERBOSE_MAKEFILE on)
set(lib_build_DIR $ENV{HOME}/tmp)
file(MAKE_DIRECTORY ${lib_build_DIR})

# configure import libs
set(dist_DIR ${CMAKE_SOURCE_DIR}/../../../dist)

set(ffmpeg_CONFIG_DIR ${CMAKE_SOURCE_DIR}/${ANDROID_ABI}/ffmpeg)
set(ffmpeg_DIR ${CMAKE_SOURCE_DIR}/ffmpeg)

add_library(libboost_thread STATIC IMPORTED )
set_target_properties(libboost_thread PROPERTIES IMPORTED_LOCATION
    ${dist_DIR}/boost/boost_1_53_0/${ANDROID_ABI}/lib/libboost_thread-gcc-mt-1_53.a)

add_library(libboost_system STATIC IMPORTED )
set_target_properties(libboost_system PROPERTIES IMPORTED_LOCATION
    ${dist_DIR}/boost/boost_1_53_0/${ANDROID_ABI}/lib/libboost_system-gcc-mt-1_53.a)

add_library(lib_ffmpeg SHARED IMPORTED )
set_target_properties(lib_ffmpeg PROPERTIES IMPORTED_LOCATION
    ${dist_DIR}/ffmpeg/lib/${ANDROID_ABI}/libffmpeg.so)


include_directories(
    ${ffmpeg_CONFIG_DIR}
    ${ffmpeg_DIR}
    ${dist_DIR}/ffmpeg/include/${ANDROID_ABI}
    ${dist_DIR}/boost/boost_1_53_0/include
)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -frtti -fexceptions")
add_library(avane-lib SHARED avane-lib.cpp jsoncpp.cpp ${ffmpeg_DIR}/ffmpeg.c ${ffmpeg_DIR}/cmdutils.c ${ffmpeg_DIR}/ffmpeg_opt.c ${ffmpeg_DIR}/ffmpeg_filter.c)
find_library( log-lib log )
target_link_libraries( avane-lib ${log-lib} libboost_system libboost_thread lib_ffmpeg )
