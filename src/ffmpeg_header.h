//
// Created by 李林超 on 2020/9/21.
//
#ifndef __FFMPEG_HEADER_H__
#define __FFMPEG_HEADER_H__

#ifdef __cplusplus
extern "C" {
#endif

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavutil/time.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_main.h>

#ifdef __cplusplus
};
#endif

#endif