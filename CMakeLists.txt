cmake_minimum_required(VERSION 3.10)
project(continuum)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET
    libavformat
    libavcodec
    libavutil
    libswscale
    libswresample
)

file(GLOB SOURCES "src/*.cc")

add_executable(continuum ${SOURCES})
target_link_libraries(continuum PkgConfig::FFMPEG)
target_include_directories(continuum PRIVATE src)

install(TARGETS continuum DESTINATION bin)