#include <stdio.h>
#include <sys/time.h>
#include "ffmpeg_header.h"

#define MAX_AUDIO_FRAME_SIZE 44100  // 1 second of 48khz 32bit audio

typedef struct Player {
    AVFormatContext     *pFormatCtx;
    AVCodecContext      *pCodecCtx;
    AVCodec             *pCodec;
    AVPacket            packet;
    AVFrame             *pFrame;
    struct SwrContext   *au_convert_ctx;
    int                 got_picture;
    int                 audioIndex;

    SDL_AudioSpec       wanted_spec;
    uint8_t             *out_buffer;
    int                 out_buffer_size;
    int                 out_sample_rate; 
    int                 out_channels;
    int                 buffer_index;
    int64_t             mDuration;
    enum AVSampleFormat out_sample_fmt;
}Player;

extern struct Player *player;

void  fill_audio(void *udata, Uint8 *stream, int len);
int decodeAudio();
void unref_res();