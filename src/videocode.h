//
// Created by 李林超 on 2020/9/24.
//
#ifndef __VIDEOCODE_H__
#define __VIDEOCODE_H__

#include <iostream>
#include <thread>
#include "packetqueue.h"
#include "framequeue.h"
#include <sys/time.h>
#include "IHandAudio.h"

#define AV_SYNC_THRESHOLD_MAX 0.1
#define AV_SYNC_THRESHOLD_MIN 0.04
#define SFM_REFRESH_EVENT (SDL_USEREVENT + 1)
#define SFM_BREAK_EVENT (SDL_USEREVENT + 2)
static const double SYNC_THRESHOLD = 0.01;
static const double NOSYNC_THRESHOLD = 10.0;
extern char av_error[AV_ERROR_MAX_STRING_SIZE];
#define averr2str(errnum) av_make_error_string(av_error, AV_ERROR_MAX_STRING_SIZE, errnum)

#define PCM 1

class VideoState {
public:
    VideoState();
    ~VideoState();
    enum AVCodecID encodetype(std::string argv);
    int videodecode(AVFormatContext *format, int videoindex, char *url, std::string output);
    int videoencode(AVFormatContext *format, char **argv);
    void videodecoder();
    void saveYUV420PFrame(AVFrame *frame);
    void Display();
    int video_refresh(int t);
    int flush_encoder(AVFormatContext *format, int index);
    void getYUV420PFrame(AVFrame *frame, FILE *fp);
    double synchronize(AVFrame *frame, double pts);
    static std::shared_ptr<VideoState> getInstance();
    void addObserver(std::shared_ptr<IHandAudio> observe);

public:
    //decode
    AVFormatContext *pFormatCtx;
    AVCodecContext *pCodecCtx;
    AVCodec *pCodec;
    AVStream *stream;
    AVPacket packet;
    AVFrame *frame;
    AVFrame *frameYUV;
    struct SwsContext *vpconvertctx;
    enum AVSampleFormat out_sample_fmt;
    uint8_t *outbuffer;

    SDL_Window *screen;
    SDL_Renderer *sdlrender;
    SDL_Texture *sdltexture;
    SDL_Rect sdlrect;
    int screen_w;
    int screen_h;
    double videoclock;
    double framedelay;
    double framelastpts;
    std::shared_ptr<PacketQueue> videoq;
    std::shared_ptr<FrameQueue> frameq;
    std::thread th1;
    int videoindex;
    std::mutex syncmutex;
    std::shared_ptr<IHandAudio> ob;
    FILE *fp;

    //encode
    AVFormatContext *eformatCtx;
    AVCodecContext *ecodecCtx;
    AVCodec *ecodec;
    AVFrame *eframe;
    AVStream *estream;
    AVPacket *epacket;
    int frameWidth;
    int frameHeight;
    int bitrate;
    FILE *inputfile;
};

#endif __VIDEOCODE_H__