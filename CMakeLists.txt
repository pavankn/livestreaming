cmake_minimum_required(VERSION 3.17)
project(MyFFmpegProject CXX)

# Find PkgConfig module
find_package(PkgConfig REQUIRED)
pkg_check_modules(RTMP REQUIRED librtmp)

# Use pkg-config to find the FFmpeg libraries and their headers
# List the required FFmpeg libraries. 'libavcodec' is the core codec library.
# You'll also need 'libavformat' for handling file formats and 'libavutil'
# for utility functions. 'libswscale' is often needed for video scaling.
pkg_check_modules(FFMPEG_LIBS REQUIRED IMPORTED_TARGET
    libavdevice
    libavformat
    libavcodec
    libavutil
    libswscale
)

# Add your executable
add_executable(livestream Giramelle.cpp)
target_include_directories(livestream PRIVATE ${RTMP_INCLUDE_DIRS})

# Link the executable against the FFmpeg libraries using the PkgConfig imported target
target_link_libraries(livestream PRIVATE ${RTMP_LIBRARIES} PkgConfig::FFMPEG_LIBS)
