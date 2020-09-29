#include <stdio.h>
#include <stdbool.h>

#include "ffmpeg_header.h"
#include <semaphore.h>
#include <pthread.h>
#include <unistd.h>


#define SFM_REFRESH_EVENT (SDL_USEREVENT + 1)
#define SFM_BREAK_EVENT (SDL_USEREVENT + 2)
#define PACKET_MAX_SIZE 1

typedef struct streampacket {
    AVPacket            packet;
    int64_t             dts;
    int64_t             pts;
    int                 state;
}streampacket;

typedef struct streampacketArray {
    unsigned            int rindex;
    unsigned            int windex;
    int                 pack;
    streampacket        packetarray[PACKET_MAX_SIZE];
}streampacketArray;

typedef struct streamframe {
    AVFrame             frame;
    AVFrame             frameyuv;
    int state;
}streamframe;

typedef struct framequeue {
    int rindex;
    int windex;
    streamframe frame_array[PACKET_MAX_SIZE];
}framequeue;

typedef struct VideoCtl {
    AVFormatContext     *pFormatCtx;
    AVCodecContext      *pCodecCtx;
    AVCodec             *pCodec;
    AVStream            *pStream;
    AVFrame             *pFrame;
    AVFrame             *pFrameYUV;
    struct SwsContext   *vp_convert_ctx;
    enum AVSampleFormat out_sample_fmt;

    /*** SDL struct ***/
    SDL_Window          *screen;
    SDL_Renderer        *sdlrender;
    SDL_Texture         *sdltexture;
    SDL_Rect            sdlrect;
    SDL_Thread          *thread;
    SDL_Thread          *sdl_event_thread;
    SDL_Event           event;

    /*** private params ***/
    unsigned char       *out_buffer;
    int                 out_buffer_size;
    int                 out_sample_rate; 
    int                 out_channels;
    int                 buffer_index;
    int                 got_picture;
    int                 videoindex;
    int                 screen_w;
    int                 screen_h;
    int                 video_count;
    sem_t               video_refresh;
    int                 refresh_time;
    uint64_t            last_frame_time;
    uint64_t            current_frame_time;
    streampacketArray   Video;
    uint64_t            last_frame_pts;
    double              video_clock;
    framequeue          fq;
    pthread_mutex_t     mutex;
}VideoCtl;


typedef struct AudioCtl {
    AVFormatContext     *pFormatCtx;
    AVCodecContext      *pCodecCtx;
    AVCodec             *pCodec;
    AVStream            *pStream;
    AVFrame             *pFrame;
    AVFrame             *pFrameYUV;

    struct SwrContext   *au_convert_ctx;
    enum AVSampleFormat out_sample_fmt;
    SDL_AudioSpec       wanted_spec;
    SDL_Thread          *thread;
    SDL_Thread          *sdl_event_thread;
    SDL_Event           event;
    uint8_t             *out_buffer;
    int                 out_buffer_size;
    int                 out_sample_rate; 
    int                 out_channels;
    int                 buffer_index;
    int                 got_picture;
    int                 audioindex;
    int                 test;
    Uint8               *audio_chunk;
    Uint32              audio_len;
    Uint8               *audio_pos;
    streampacketArray   Audio;
    uint64_t            last_frame;
    double              audio_clock;
    double              audio_pts;
}AudioCtl;

int audio_decode_frame();
