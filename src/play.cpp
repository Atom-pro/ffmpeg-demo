//
// Created by 李林超 on 2020/9/22.
//

#include "play.h"
AVFormatContext *pFormatCtx;
FILE *out_pcm;
FILE* mp3file;
FILE *input;
int ret = 0;
std::string encode_input_pcm_decode_output_pcm = "./3.pcm";
std::string encode_output_mp3 = "./3.mp3";
std::string output = "./2.yuv";
#define PCM 1

//extern char aver[AV_ERROR_MAX_STRING_SIZE] = { 0 };
//#define averr2str(errnum) av_make_error_string(aver, AV_ERROR_MAX_STRING_SIZE, errnum) // 编译 averr2str 会报错引用临时变量

AudioState::AudioState()
        :audioindex(-1)
        ,videoindex(-1)
        ,duration(0)
        ,bufferindex(0)
{
    videostate = VideoState::getInstance();
}

AudioState::~AudioState() {
    if (th1.joinable())
        th1.join();
    fclose(out_pcm);
    if (src_data)
        av_freep(&src_data[0]);
    av_freep(&src_data);
    if (dst_data)
        av_freep(&dst_data[0]);
    av_freep(&dst_data);
    if (encode_swr_ctx)
        swr_free(&encode_swr_ctx);
    if (mp3file)
        fclose(mp3file);
    if (input)
        fclose(input);
    if (frame)
        av_frame_free(&frame);
    if (pkt)
        av_packet_free(&pkt);
    if (codecCtx)
        avcodec_free_context(&codecCtx);
}
std::shared_ptr<AudioState> AudioState::mInstance = nullptr;
std::shared_ptr<AudioState> AudioState::getInstance() {
    if (mInstance == nullptr) {
        std::mutex mutex;
        std::lock_guard<std::mutex> lock(mutex);
        mInstance = std::shared_ptr<AudioState>(new AudioState);
    }
    return mInstance;
}

int AudioState::check_sample_fmt(const AVCodec *codec, enum AVSampleFormat sample_fmt) {
    const enum AVSampleFormat *p = codec->sample_fmts;
    while (*p != AV_SAMPLE_FMT_NONE) {
        if (*p == sample_fmt)
            return 1;
        p++;
    }
    return 0;
}

int AudioState::select_channel_layout(const AVCodec *codec) {
    const uint64_t *p;
    uint64_t best_ch_layout = 0;
    int best_nb_channels   = 0;

    if (!codec->channel_layouts)
        return AV_CH_LAYOUT_STEREO;

    p = codec->channel_layouts;
    while (*p) {
        int nb_channels = av_get_channel_layout_nb_channels(*p);

        if (nb_channels > best_nb_channels) {
            best_ch_layout    = *p;
            best_nb_channels = nb_channels;
        }
        p++;
    }
    return best_ch_layout;
}

int AudioState::select_sample_rate(const AVCodec *codec) {
    const int *p;
    int best_samplerate = 0;

    if (!codec->supported_samplerates) // 编码器支持的采样率
        return 44100;

    p = codec->supported_samplerates;
    while (*p) {
        if (!best_samplerate || abs(44100 - *p) < abs(44100 - best_samplerate))
            best_samplerate = *p;
        p++;
    }
    return best_samplerate;
}

void AudioState::encode(AVCodecContext *codecCtx, AVFrame *frame, AVPacket *pkt, FILE *fp) {
    ret = avcodec_send_frame(codecCtx, frame);
    if (ret < 0) {
        fprintf(stderr, "avcodec_send_frame failed: %s\n", averr2str(ret));
        return;
    }
    while (ret >= 0) {
        ret = avcodec_receive_packet(codecCtx, pkt);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            avcodec_flush_buffers(codecCtx);
            return;
        }
        else if (ret < 0) {
            fprintf(stderr, "avcodec_receive_packet failed\n");
            return;
        }
        fwrite(pkt->data, 1, pkt->size, fp);
        av_packet_unref(pkt);
    }
}

void AudioState::audio_encode(char **argv) {
    av_register_all();
//    AVCodec *id = av_codec_next(nullptr);
//    while (id) {
//        std::this_thread::sleep_for(std::chrono::microseconds(10000));
//        printf("id.name = %d, %d\n", id->id, AV_CODEC_ID_MP3);
//
//        if (id->id == AV_CODEC_ID_MP3) {
//            if (id->encode2 != nullptr)
//                break;
//        }
//        id = id->next;
//    }

    codec = avcodec_find_encoder(AV_CODEC_ID_MP3);
    if (codec == nullptr) {
        fprintf(stderr, "avcodec_find_encoder failed audioencode\n");
        return;
    }
    codecCtx = avcodec_alloc_context3(codec);
    if (codecCtx == nullptr) {
        fprintf(stderr, "avcodec_alloc_context3 failed\n");
        return;
    }
    codecCtx->bit_rate = 64000;
    codecCtx->sample_fmt = AV_SAMPLE_FMT_S16P;
    if (!check_sample_fmt(codec, codecCtx->sample_fmt)) {
        fprintf(stderr, "encode not support sample format: %s\n", av_get_sample_fmt_name(codecCtx->sample_fmt));
        return;
    }
    codecCtx->sample_rate = select_sample_rate(codec);
    codecCtx->channel_layout = select_channel_layout(codec);
    codecCtx->channels = av_get_channel_layout_nb_channels(codecCtx->channel_layout);

    ret = avcodec_open2(codecCtx, codec, nullptr); //分配内存，调用 init, 编解码监察，编码格式是否符合要求
    if (ret < 0) {
        fprintf(stderr, "avcodec_open2 failed: %s\n", averr2str(ret));
        return;
    }
    mp3file = fopen(argv[3], "ab");
    if (!mp3file) {
        fprintf(stderr, "fopen failed\n");
        return;
    }

    pkt = av_packet_alloc();
    if (!pkt) {
        fprintf(stderr, "av_packet_alloc failed\n");
        return;
    }

    // 初始化存放 PCM 数据的 frame
    frame = av_frame_alloc();
    if (!frame) {
        fprintf(stderr, "av_frame_alloc failed\n");
        return;
    }

    frame->nb_samples = codecCtx->frame_size;
    frame->format = codecCtx->sample_fmt;
    frame->channel_layout = codecCtx->channel_layout;

    ret = av_frame_get_buffer(frame, 0);
    if (ret < 0) {
        fprintf(stderr, "av_frame_get_buffer: %s\n", averr2str(ret));
        return;
    }

    // 重采样设置
    encode_swr_ctx = swr_alloc();
    if (!encode_swr_ctx) {
        fprintf (stderr, "swr_alloc failed\n");
        return;
    }
    av_opt_set_int(encode_swr_ctx, "in_channel_layout", AV_CH_LAYOUT_STEREO, 0);
    av_opt_set_int(encode_swr_ctx, "in_sample_rate", 44100, 0);
    av_opt_set_int(encode_swr_ctx, "in_sample_fmt", AV_SAMPLE_FMT_S16, 0);

    av_opt_set_int(encode_swr_ctx, "out_channel_layout", AV_CH_LAYOUT_STEREO, 0);
    av_opt_set_int(encode_swr_ctx, "out_sample_rate", 44100, 0);
    av_opt_set_int(encode_swr_ctx, "out_sample_fmt", AV_SAMPLE_FMT_S16P, 0);

    ret = swr_init(encode_swr_ctx);
    if (ret < 0) {
        fprintf(stderr, "swr_init failed\n");
        return;
    }
    src_nb_channels = av_get_channel_layout_nb_channels(AV_CH_LAYOUT_STEREO);
    // 分配样本空间  buff 大小计算方式： nb_samples（采样点数）* sample_fmt（2字节）* nb_channels 2 * 2 * 1152 = 4608
    ret = av_samples_alloc_array_and_samples(&src_data, &src_linesize, src_nb_channels, src_nb_samples, AV_SAMPLE_FMT_S16, 0);
    if (ret < 0) {
        fprintf(stderr, "av_samples_alloc_array_and_samples failed: %s\n", averr2str(ret));
        return;
    }
    maxdstNbSamples = dstNbSamples = av_rescale_rnd(src_nb_samples, 44100, 44100, AV_ROUND_UP);
    dst_nb_channels = av_get_channel_layout_nb_channels(AV_CH_LAYOUT_STEREO);
    ret = av_samples_alloc_array_and_samples(&dst_data, &dst_linesize, dst_nb_channels,
                                             dstNbSamples, AV_SAMPLE_FMT_S16P, 0);
    if (ret < 0) {
        fprintf(stderr, "av_samples_alloc_array_and_samples failed: %s\n", averr2str(ret));
        return;
    }

    input = fopen(argv[1], "rb");
    if (!input) {
        fprintf(stderr, "fopen input failed\n");
        return;
    }
    uint8_t *tmpdata = frame->data[0]; // 左声道数据
    uint8_t *tmpdata1 = frame->data[1]; // 右声道数据
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    uint64_t start_time_sec = tv.tv_sec;
    uint64_t start_time_usec = tv.tv_usec;
    while (!feof(input)){
        int readsize = fread(src_data[0], 1, src_nb_channels*src_nb_samples*sizeof(uint16_t), input); // 源数据大小
        src_nb_samples = readsize / (src_nb_channels*sizeof(uint16_t));
        if (!readsize)
            break;
        dstNbSamples = av_rescale_rnd(swr_get_delay(encode_swr_ctx, 44100) + src_nb_samples, 44100, 44100, AV_ROUND_UP);
        if (dstNbSamples > maxdstNbSamples) {
            av_freep(&dst_data[0]);
            ret = av_samples_alloc(dst_data, &dst_linesize, dst_nb_channels,
                                   dstNbSamples, AV_SAMPLE_FMT_S16P, 1);
            if (ret < 0)
                break;
            maxdstNbSamples = dstNbSamples;
        }

        ret = swr_convert(encode_swr_ctx, dst_data, dstNbSamples, (const uint8_t **)src_data, src_nb_samples);
        if (ret < 0) {
            fprintf(stderr, "Error while converting\n");
            return;
        }
        frame->nb_samples = dstNbSamples;
        codecCtx->frame_size = dstNbSamples;
        frame->data[0] = dst_data[0];
        frame->data[1] = dst_data[1];

        encode(codecCtx, frame, pkt, mp3file);
        fprintf(stdout, "encode data size:%d\n",readsize);
    }
    encode(codecCtx, nullptr, pkt, mp3file);
    frame->data[0] = tmpdata;
    frame->data[1] = tmpdata1;
    gettimeofday(&tv, nullptr);
    uint64_t end_time_sec = tv.tv_sec;
    uint64_t end_time_usec = tv.tv_usec;
    uint64_t time = end_time_sec - start_time_sec;
    time += time * 1000000 + (end_time_usec - start_time_usec);
    fprintf(stdout, "编码时间: %ld 毫秒\n", time/1000);
}

int AudioState::decodeAudio() {
    ret = 0;
    int swr_size = 0;
    int dstNbSamples;
    int dst_bufsize = 0;
    AVPacket packet;
    AVFrame *frame;
    frame = av_frame_alloc();
    while (1) {
        audioq->get(&packet);
        if (packet.pts != AV_NOPTS_VALUE)
            audioclock = av_q2d(stream->time_base) * packet.pts;

        if (packet.stream_index == audioindex) {
            while (1) {
                ret = avcodec_receive_frame(pCodecCtx, frame);
                if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
                    fprintf(stderr, "avcodec_receive_frame faild, ret : %s\n", averr2str(ret));
                    break;
                }
                else if (ret == AVERROR_EOF) {
                    fprintf(stderr, "AVERROR_EOR: %s\n", averr2str(ret));
                    avcodec_flush_buffers(pCodecCtx);
                }
                else if (ret == AVERROR(EAGAIN))
                    break;
                else {
                    dstNbSamples = av_rescale_rnd(swr_get_delay(au_convert_ctx, pCodecCtx->sample_rate) +
                                                    1024,44100, pCodecCtx->sample_rate, AV_ROUND_UP);
                    ret = av_samples_alloc(&(out_buffer), nullptr, 2,dstNbSamples, AV_SAMPLE_FMT_S16, 1);
                    swr_size = swr_convert(au_convert_ctx, &( out_buffer), dstNbSamples, (const uint8_t **)(frame->data), frame->nb_samples);
                    if (swr_size < 0) {
                        fprintf(stderr, "swr_convert failed: %d\n", swr_size);
                        break;
                    }
                    dst_bufsize = av_samples_get_buffer_size(nullptr, 2, swr_size, AV_SAMPLE_FMT_S16, 1);
                    if (dst_bufsize < 0) {
                        dst_bufsize = 0;
                        break;
                    }
                    audioclock += static_cast<double>(dst_bufsize) / (2 * stream->codecpar->channels * stream->codecpar->sample_rate);
                    setaudioclock(audioclock);
                    fprintf(stdout, "audioclock: %f, stream->codecpar->sample_rate: %d\n", audioclock, stream->codecpar->sample_rate);
                }
            }

            ret = avcodec_send_packet(pCodecCtx, &packet);
            if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
                fprintf(stderr, "avcodec_send_packet failed, ret : %s\n", averr2str(ret));
            audiolen = dst_bufsize;
            audiopos = static_cast<Uint8*>(out_buffer);
            break;
        }
        av_packet_unref(&(packet));
    }
}

void fill_audio(void *opaque, uint8_t *stream, int len)
{
    AudioState *audioState = static_cast<AudioState*>(opaque);
    int available = 0;
    int write_len = 0;
    int written_len = 0;
    SDL_memset(stream, 0, len);
    while (len > 0) {
        if (audioState->bufferindex >= audioState->audiolen) {
            audioState->bufferindex = 0;
            audioState->decodeAudio();
            continue;
        }

        available = audioState->audiolen - audioState->bufferindex;
        write_len = available > len ? len : available;

        memcpy(stream, audioState->audiopos + audioState->bufferindex, write_len);
#ifdef PCM
        fwrite(audioState->audiopos + audioState->bufferindex, 1, write_len, out_pcm);
#endif
        stream += write_len;
        audioState->bufferindex += write_len;
        len -= write_len;
    }
}

void AudioState::setaudioclock(double clock) {
    std::lock_guard<std::mutex> lock(clockmutex);
    audioclock = clock;
}

double AudioState::getAudioClock() {
    std::lock_guard<std::mutex> lock(clockmutex);
    return audioclock;
}

bool AudioState::audio_play() {
#ifdef PCM
    out_pcm = fopen(encode_input_pcm_decode_output_pcm.c_str(), "wb");
    if (out_pcm == nullptr) {
        fprintf(stderr, "open out_pcm failed\n");
        return -1;
    }
#endif
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
        fprintf(stderr, "SDL init failed,  ErrorInfo = %s\n", SDL_GetError());
        return -1;
    }
    // 封装 SDL_AudioSpec 结构体
    wanted_spec.freq = 44100;          // 每秒向音频设备发送的 sample 数据，采样率越大质量越好
    wanted_spec.format = AUDIO_S16SYS; // 本地音频字节序
    wanted_spec.channels = 2;
    wanted_spec.silence = 0;           // 设置静音值
    wanted_spec.samples = 1024;        // 音频缓冲区大小, format * channels
    wanted_spec.callback = fill_audio; // 回调函数，获取音频码流送个输出设备
    wanted_spec.userdata = this;

    ret = SDL_OpenAudio(&wanted_spec, nullptr);
    if (ret < 0) {
        fprintf(stderr, "SDL_OpenAudio failed: %s\n", averr2str(ret));
        return -1;
    }

    // 音频重采样
    au_convert_ctx = swr_alloc();
    if (au_convert_ctx == nullptr) {
        fprintf(stderr, "swr_alloc failed\n");
        return -1;
    }
    fprintf(stdout, "pCodecCtx->channel_layout:%lu pCodecCtx->channels:%d\n", pCodecCtx->channel_layout, pCodecCtx->channels);

    /* set options */
    if (pCodecCtx->channel_layout == 0)
        av_opt_set_int(au_convert_ctx, "in_channel_layout", av_get_default_channel_layout(pCodecCtx->channels), 0);
    else
        av_opt_set_int(au_convert_ctx, "in_channel_layout", pCodecCtx->channel_layout, 0);

    av_opt_set_int(au_convert_ctx, "in_sample_rate", pCodecCtx->sample_rate, 0);
    av_opt_set_sample_fmt(au_convert_ctx, "in_sample_fmt", pCodecCtx->sample_fmt, 0);

    av_opt_set_int(au_convert_ctx, "out_channel_layout", AV_CH_LAYOUT_STEREO, 0);
    av_opt_set_int(au_convert_ctx, "out_channels", 2, 0);
    av_opt_set_int(au_convert_ctx, "out_sample_rate", 44100, 0);
    av_opt_set_sample_fmt(au_convert_ctx, "out_sample_fmt", AV_SAMPLE_FMT_S16, 0);

    swr_init(au_convert_ctx);

    // 播放
    SDL_PauseAudio(0);
}

void AudioState::decode() {
    AVPacket packet;
    while (1) {
        ret = av_read_frame(pFormatCtx, &packet);
        if (ret < 0) {
            if (ret == AVERROR_EOF) {
                fprintf(stdout, "av_read_frame AVERROR_EOF: %s\n", averr2str(ret));
#ifdef PCM
                fclose(out_pcm);
#endif
                break;
            }
        }
        if (packet.stream_index == audioindex) {
            audioq->put(&packet);
            av_packet_unref(&packet);
        } else if (packet.stream_index == videoindex) {
            videostate->videoq->put(&packet);
            av_packet_unref(&packet);
        }
    }
}

void AudioState::begin() {
    th1 = std::thread(&AudioState::decode, this);
}

int AudioState::audio_decode(char **argv) {
    ret = 0;
    pFormatCtx = avformat_alloc_context();
    ret = avformat_open_input(&pFormatCtx, argv[1], nullptr, nullptr);
    if (ret < 0) {
        fprintf(stderr, "avformat_open_input: %s\n", averr2str(ret));
        return -1;
    }

    ret = avformat_find_stream_info(pFormatCtx, nullptr);
    if (ret < 0) {
        fprintf(stderr, "avformat_find_stream_info: %s\n", averr2str(ret));
        return -1;
    }

    av_dump_format(pFormatCtx, -1, argv[1], 0);

    audioindex = av_find_best_stream(pFormatCtx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    videoindex = av_find_best_stream(pFormatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (audioindex == -1) {
        fprintf(stderr, "get stream index failed\n");
        return -1;
    }
    pCodecCtx = avcodec_alloc_context3(nullptr);
    if (pCodecCtx == nullptr) {
        fprintf(stderr, "avcodec_alloc_context3 failed\n");
        return -1;
    }
    // 将音频流信息导入到 pCodecCtx 中
    ret = avcodec_parameters_to_context(pCodecCtx, pFormatCtx->streams[audioindex]->codecpar);
    if (ret < 0) {
        fprintf(stderr, "avcodec_parameters_to_context failed: %s\n", averr2str(ret));
        return -1;
    }
    pCodec = avcodec_find_decoder(pCodecCtx->codec_id);
    if (pCodec == nullptr) {
        fprintf(stderr, "avcodec_find_decoder failed\n");
        return -1;
    }

    ret = avcodec_open2(pCodecCtx, pCodec, nullptr);
    if (ret < 0) {
        fprintf(stderr, "avcodec_open2 failed: %s\n", averr2str(ret));
        return -1;
    }

    duration = pFormatCtx->duration / 1000;
    fprintf(stdout, "duration: %f\n", duration/1000);
    stream = pFormatCtx->streams[audioindex];
    videostate->stream = pFormatCtx->streams[videoindex];

    frame_timer = static_cast<double>(av_gettime()) / 1000000;
    frame_last_delay = 0.0;
    audioq = std::shared_ptr<PacketQueue>(new PacketQueue);
    begin();
    audio_play();
    videostate->videodecode(pFormatCtx, videoindex, argv[1], output);
}

void dumpTrace(int signal) {
    void *buffer[16];
    char **strings;
    int ret = backtrace(buffer, 16);
    strings = backtrace_symbols(buffer, ret);
    if (strings == nullptr) {
        fprintf(stderr, "backtrace_symbol failed\n");
        return;
    }
    for (int i=0; i<ret; ++i){
        fprintf(stdout, "[%02d] %s\n", i, strings[i]);
        free(strings);
    }
}

void signalHandler(int sig) {
    fprintf(stderr, "receive signal : %d\n", sig);
    dumpTrace(sig);
}

void registersignal() {
    signal(SIGQUIT, signalHandler);
    signal(SIGILL, signalHandler);
    signal(SIGTRAP, signalHandler);
    signal(SIGABRT, signalHandler);
    signal(SIGKILL, signalHandler);
    signal(SIGSEGV, signalHandler);
    signal(SIGFPE, signalHandler);
    signal(SIGTERM, signalHandler);
    signal(SIGSTOP, signalHandler);
}

void signalHandle() {
    registersignal();
}

int main(int argc, char *argv[]){
    std::shared_ptr<AudioState> mAudiostate = AudioState::getInstance();
    fprintf(stdout, "argc : %d, argv[2] : %s\n", argc, argv[2]);
//    signalHandle();
    if (argc == 4 && !strcmp(argv[2], "encode")) {
        mAudiostate->audio_encode(argv);
        return 0;
    } else if (argc == 5 && !strcmp(argv[2], "encode")) {
        mAudiostate->videostate->videoencode(pFormatCtx, argv);
    } else if (!strcmp(argv[2], "decode")) {
        mAudiostate->audio_decode(argv);
        SDL_Event event;
        while (1) {
            SDL_WaitEvent(&event);
            switch (event.type) {
                case SDL_QUIT:
                    SDL_Quit();
                    return 0;
                case SFM_REFRESH_EVENT:
                    mAudiostate->videostate->Display();
                    break;
                default:
                    break;
            }
        }
    }
    return 0;
}