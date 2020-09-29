//
// Created by 李林超 on 2020/9/22.
//

#ifndef DEMO_VIDEOH_H
#define DEMO_VIDEOH_H

#include <iostream>
#include <thread>
#include <mutex>
#include <string>
#include <unistd.h>
#include "ffmpeg_header.h"
using namespace std;

#define SFM_REFRESH_EVENT (SDL_USEREVENT + 1)
#define SFM_BREAK_EVENT (SDL_USEREVENT + 2)
#define PACKET_MAX_SIZE 128

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

class AVState{
public:
    ~AVState();

    static std::shared_ptr<AVState> getInstance();
    int avstate_init(std::string url);
    void event_loop();
    void audio_thread();
    void sdlrefreshthread();
    void decodepacket();
    void videodecodeframe();

    bool IsPacketArrayFull(streampacketArray is);
    bool IsPacketArrayEmpty(streampacketArray is);
    bool IsFrameFull(framequeue is);
    bool IsFrameEmpty(framequeue is);
    AVFormatContext *pFormatCtx;


private:
    AVState();

public:
    int videoindex;
    int audioindex;
    AVCodecContext *pAudioCodecCtx;
    AVCodecContext *pVideoCodecCtx;
    AVCodec *pAudioCodec;
    AVCodec *pVideoCodec;
    AVPacket *packet;
    int thread_exit;
    int thread_pause;
    double pts;

    // Audio
    SDL_AudioSpec wanted_spec;
    AVFrame *pAudioFrame;
    struct SwrContext   *AuconvertCtx;
    streampacketArray   Audio;
    Uint32 AudioLen;
    Uint8 *AudioPos;
    uint8_t *AudioOutBuffer;
    double audioclock;

    // Video
    int screen_w;
    int screen_h;
    AVFrame *pVideoFrame;
    AVFrame *pVideoFrameYUV;
    unsigned char *VideoOutBuffer;
    struct SwsContext *VpconvertCtx;
    streampacketArray Video;
    framequeue fq;
    double videoclock;

    // SDL

    SDL_Window          *screen;
    SDL_Renderer        *sdlrender;
    SDL_Texture         *sdltexture;
    SDL_Rect            sdlrect;
    SDL_Thread *thread;

    // thread
    std::thread th1;
    std::thread th2;
    std::thread srt;
    std::thread dpt;
    std::thread dft;
    static std::shared_ptr<AVState> mInstance;
};


#endif //DEMO_VIDEOH_H
