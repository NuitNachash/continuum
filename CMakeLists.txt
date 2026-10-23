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
    libavfilter
)
set(SOURCES
    src/main.cc
    src/ContinuumEngine.cc
    src/Encoder.cc
    src/Streamer.cc
    src/VideoFramesource.cc
    src/AudioFramesource.cc 
    src/TimelineManager.cc
    src/PlaylistManager.cc
    src/logger.cc
    src/ClockSync.cc
    src/FrameWriter.cc
    src/Config.cc

)

add_executable(continuum ${SOURCES})
target_link_libraries(continuum PkgConfig::FFMPEG)
target_include_directories(continuum PRIVATE src)

install(TARGETS continuum DESTINATION bin)