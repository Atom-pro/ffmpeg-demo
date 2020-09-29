cmake_minimum_required(VERSION 3.4.1)

project(demo)

set(ExecuteName demo)
set(CMAKE_BUILD_TYPE Debug)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -pthread")
set(FFMPEG_INCLUDE_DIR /home/newuser/gaoding/FFmpeg)
set(FFMPEG_LIBRARY_DIR /home/newuser/gaoding/FFmpeg)

#aux_source_directory(./src DIR_SRC)

# FFmpeg
IF (CMAKE_SYSTEM_NAME MATCHES "Darwin")
find_path(AVFORMAT_INCLUDE_DIR libavformat/avformat.h)
find_library(AVFORMAT_LIBRARY avformat)

find_path(AVCODEC_INCLUDE_DIR libavcodec/avcodec.h)
find_library(AVCODEC_LIBRARY avcodec)

find_path(AVUTIL_INCLUDE_DIR libavutil/avutil.h)
find_library(AVUTIL_LIBRARY avutil)

find_path(AVFILTER_INCLUDE_DIR libavfilter/avfilter.h)
find_library(AVFILTER_LIBRARY avfilter)

find_path(SWSCALE_INCLUDE_DIR libswscale/swscale.h)
find_library(SWSCALE_LIBRARY swscale)

find_path(SWRESAMPLE_INCLUDE_DIR libswresample/swresample.h)
find_library(SWRESAMPLE_LIBRARY swresample)

find_path(SDL_INCLUDE_DIR SDL2/SDL.h)
find_library(SDL_LIBRARY SDL2)
find_library(SDL_MAIN_LIBRARY SDL2main)

set(EXTRAL_INCLUDE
        ${AVFORMAT_INCLUDE_DIR}
        ${AVCODEC_INCLUDE_DIR}
        ${AVUTIL_INCLUDE_DIR}
        ${AVFILTER_INCLUDE_DIR}

        ${SWRESAMPLE_INCLUDE_DIR}
        ${SDL_INCLUDE_DIR})
include_directories(${EXTRAL_INCLUDE})

set(EXTRAL_LIBRARY
        ${AVFORMAT_LIBRARY}
        ${AVCODEC_LIBRARY}
        ${AVUTIL_LIBRARY}
        ${AVFILTER_LIBRARY}
        ${SWSCALE_LIBRARY}
        ${SWRESAMPLE_LIBRARY}
        ${SDL_LIBRARY}
        ${SDL_MAIN_LIBRARY})

SET(SRC_MEDIA ${DIR_SRC})

# 生成可执行文件
add_executable(${ExecuteName} src/play.h src/play.cpp src/packetqueue.h src/packetqueue.cpp src/videocode.cpp src/videocode.h src/framequeue.h src/framequeue.cpp)
#add_executable(${ExecuteName} src/video.c)

# 添加链接库
target_link_libraries(${ExecuteName} ${EXTRAL_LIBRARY})

ELSEIF(CMAKE_SYSTEM_NAME MATCHES "Linux")
    message("==== " ${FFMPEG_LIBRARY_DIR})
    include_directories(${FFMPEG_INCLUDE_DIR} ${FFMPEG_INCLUDE_DIR}/libswresample /usr/include/SDL2)
    link_directories(${FFMPEG_LIBRARY_DIR} ${FFMPEG_LIBRARY_DIR}/libswresample ${FFMPEG_LIBRARY_DIR}/libavutil ${FFMPEG_LIBRARY_DIR}/libavcodec ${FFMPEG_LIBRARY_DIR}/libavformat)

    add_executable(${ExecuteName} src/play.h src/play.cpp src/ffmpeg_header.h src/packetqueue.h src/packetqueue.cpp src/videocode.cpp src/videocode.h src/framequeue.h src/framequeue.cpp src/IHandAudio.h)
    target_link_libraries(${ExecuteName} avformat avcodec avutil swresample swscale SDL2 SDL2main vdpau X11 z m va va-drm va-x11)
ENDIF()