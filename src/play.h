//
// Created by 李林超 on 2020/9/22.
//

#include <iostream>
#include <thread>
#include <mutex>
#include <queue>
#include <sys/time.h>
#include <condition_variable>
#include "videocode.h"
#include <string.h>
#include <signal.h>
#include <execinfo.h>

class AudioState
        : public std::enable_shared_from_this<AudioState>
        , public IHandAudio{
public:
    AudioState();
    ~AudioState();

    static std::shared_ptr<AudioState> getInstance();
    int audio_decode(char **url);
    void audio_encode(char **argv);
    void begin();
    bool audio_play();
    int decodeAudio();
    void decode();
    void encode(AVCodecContext *codecCtx, AVFrame *frame, AVPacket *pkt, FILE *fp);
    static int select_sample_rate(const AVCodec *codec);
    static int select_channel_layout(const AVCodec *codec);
    static int check_sample_fmt(const AVCodec *codec, enum AVSampleFormat sample_fmt);
    void setaudioclock(double clock);
    double getAudioClock() override ;
public:
    static std::shared_ptr<AudioState> mInstance;
    std::thread th1;

    AVCodecContext *pCodecCtx;
    AVCodec *pCodec;
    AVStream *stream;
    std::shared_ptr<PacketQueue> audioq;
    int audioindex;
    SDL_AudioSpec       wanted_spec;
    struct SwrContext   *au_convert_ctx;
    double duration;
    double audioclock;
    double frame_timer;
    double frame_last_delay;
    int audiolen;
    uint8_t *audiopos;
    int bufferindex;
    uint8_t *out_buffer;
    std::mutex clockmutex;

    // encode
    uint8_t **src_data;
    uint8_t **dst_data;
    int src_linesize, dst_linesize;
    int src_nb_channels, dst_nb_channels;
    int src_nb_samples = 1152, dstNbSamples, maxdstNbSamples;
    AVCodecContext *codecCtx;
    AVCodec *codec;
    AVFrame *frame;
    AVPacket *pkt;
    struct SwrContext *encode_swr_ctx;

    // video
    int videoindex;
    std::shared_ptr<VideoState> videostate;
};