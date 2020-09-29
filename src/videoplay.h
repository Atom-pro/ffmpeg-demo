#include <stdio.h>

#include "ffmpeg_header.h"

#define SFM_REFRESH_EVENT (SDL_USEREVENT + 1)
#define SFM_BREAK_EVENT (SDL_USEREVENT + 2)

typedef struct Videoplay {
    AVFormatContext     *pFormatCtx;
    AVCodecContext      *pCodecCtx;
    AVCodec             *pCodec;
    AVPacket            *packet;
    AVFrame             *pFrame;
    AVFrame             *pFrameYUV;
    unsigned char       *out_buffer;
    struct SwsContext   *img_convert_ctx;
    SDL_Window          *screen;
    SDL_Renderer        *sdlrender;
    SDL_Texture         *sdltexture;
    SDL_Rect            sdlrect;
    SDL_Thread          *thread;
    SDL_Event           event;
    int                 got_picture;
    int                 videoindex;
    int                 screen_w;
    int                 screen_h;
}Videoplay;

extern struct Videoplay *vp;