//
// Created by 李林超 on 2020/9/22.
//

#include "videoh.h"

std::shared_ptr<AVState> AVState::mInstance = nullptr;
std::shared_ptr<AVState> AVState::getInstance() {
    if (mInstance == nullptr) {
        static std::mutex mutex;
        static lock_guard<std::mutex> lock(mutex);
        if (mInstance == nullptr) {
            mInstance = std::shared_ptr<AVState>(new AVState());
            if (mInstance == nullptr) {
                return nullptr;
            }
        }
    }
    return mInstance;
}

AVState::AVState() {

}

AVState::~AVState() {
    if (th1.joinable())
        th1.join();
    if (th2.joinable())
        th2.join();
    if (srt.joinable())
        srt.join();
    if (dft.joinable())
        dft.join();
    if (dpt.joinable())
        dpt.join();
    SDL_DestroyWindow(screen);
    SDL_CloseAudio();
    SDL_Quit();
    sws_freeContext(VpconvertCtx);
    swr_free(&AuconvertCtx);
    avcodec_close(pAudioCodecCtx);
    avcodec_close(pVideoCodecCtx);
    avformat_close_input(&pFormatCtx);
}

bool AVState::IsPacketArrayFull(streampacketArray is) {
    int i = is.windex % PACKET_MAX_SIZE;
    if (is.packetarray[i].state != 0)
        return 1;
    return 0;
}

bool AVState::IsPacketArrayEmpty(streampacketArray is) {
    int i = is.rindex % PACKET_MAX_SIZE;
    if (is.packetarray[i].state == 0)
        return 1;
    return 0;
}

bool AVState::IsFrameFull(framequeue is) {
    int i = is.windex % PACKET_MAX_SIZE;
    if (is.frame_array[i].state != 0)
        return 1;
    return 0;
}

bool AVState::IsFrameEmpty(framequeue is) {
    int i = is.rindex % PACKET_MAX_SIZE;
    if (is.frame_array[i].state == 0)
        return 1;
    return 0;
}


void fill_audio(void *opaque, uint8_t *stream, int len) {
    SDL_memset(stream, 0, len);
    while (len > 0) {
        if (AVState::getInstance()->thread_exit)
            break;
        if (AVState::getInstance()->thread_pause)
            continue;
        if (AVState::getInstance()->AudioLen == 0) {
            continue;
        }
        AVState::getInstance()->audioclock = AVState::getInstance()->AudioLen;
        fprintf(stdout, "AVState::getInstance()->audioclock: %f\n", AVState::getInstance()->audioclock);
        int temp = (len > AVState::getInstance()->AudioLen ? AVState::getInstance()->AudioLen : len);
        SDL_MixAudio(stream, AVState::getInstance()->AudioPos, temp, SDL_MIX_MAXVOLUME);
        AVState::getInstance()->AudioPos += temp;
        AVState::getInstance()->AudioLen -= temp;
        stream += temp;
        len -= temp;


    }
}

void AVState::sdlrefreshthread() {
    while (!AVState::getInstance()->thread_exit) {
        if (!AVState::getInstance()->thread_pause) {
            SDL_Event event;
            event.type = SFM_REFRESH_EVENT;
            SDL_PushEvent(&event);
        }
//        getframe(clock)
    }
    fprintf(stdout, "sdl_refresh_thread finished\n");
}

void AVState::decodepacket() {
    AVPacket pkt;
    int i = 0;

    while (1) {
        if (thread_exit) {
            fprintf(stderr, "thread_exit\n");
            break;
        }

        if (av_read_frame(pFormatCtx, &pkt) < 0) {
            // printf("av_read_frame failed, pkt.pts = %lld\n", (unsigned long long)pkt.pts);
            // thread_exit = 1;
            // SDL_Event event;
            // event.type = SFM_BREAK_EVENT;
            // SDL_PushEvent(&event);
            break;
        }

        pts = pkt.pts;
        if (pkt.stream_index == videoindex) {
            if (Video.windex >= PACKET_MAX_SIZE) {
                Video.windex = 0;
            }
            while (IsPacketArrayFull(Video)) {
                usleep(5000);
                continue;
            }

            i = Video.windex;
            Video.packetarray[i].packet = pkt;
            Video.packetarray[i].dts = pkt.dts;
            Video.packetarray[i].pts = pkt.pts;
            Video.packetarray[i].state = 1;
            Video.windex++;
        }
        if (pkt.stream_index == audioindex) {
            if (Audio.windex >= PACKET_MAX_SIZE)
                Audio.windex = 0;
            while (IsPacketArrayFull(Audio)) {
                usleep(5000);
                continue;
            }
            i = Audio.windex;
            Audio.packetarray[i].packet = pkt;
            Audio.packetarray[i].dts = pkt.dts;
            Audio.packetarray[i].pts = pkt.pts;
            Audio.packetarray[i].state = 1;
            Audio.windex++;
        }
    }
    fprintf(stdout, "decode_frame thread finished\n");
}

void AVState::audio_thread(){
    int ret = 0;
    int i = 0;
    AVPacket pkt;
    int swr_size = 0;
    int dst_nb_samples;
    int dst_bufsize = 0;
    pAudioFrame = av_frame_alloc();
    while (1) {
        if (thread_exit)
            break;
        if (thread_pause) {
            usleep(10000);
            continue;
        }

        if (IsPacketArrayEmpty(Audio)) {
            SDL_Delay(1);
            continue;
        }
        i = Audio.rindex;
        pkt = Audio.packetarray[i].packet;

        if (pkt.stream_index == audioindex) {
            ret = avcodec_send_packet(pAudioCodecCtx, &pkt);
            if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
                printf("avcodec_send_packet failed, ret : %d\n", ret);
            }
            while (1) {
                ret = avcodec_receive_frame(pAudioCodecCtx, pAudioFrame);
                if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
                    printf("avcodec_receive_frame faild, ret : %d\n", ret);
                    break;
                }
                else if (ret == AVERROR_EOF) {
                    printf("AVERROR_EOR\n");
                    avcodec_flush_buffers(pAudioCodecCtx);
                }
                else if (ret == AVERROR(EAGAIN)) {
                    break;
                }
                else
                {
                    dst_nb_samples = av_rescale_rnd(swr_get_delay(AuconvertCtx, pAudioCodecCtx->sample_rate) +
                                                    1024,44100, pAudioCodecCtx->sample_rate, AV_ROUND_UP);
                    ret = av_samples_alloc(&AudioOutBuffer, nullptr, 2,
                                           dst_nb_samples, AV_SAMPLE_FMT_S16, 1);
                    swr_size = swr_convert(AuconvertCtx, &(AudioOutBuffer), dst_nb_samples, (const uint8_t **)(pAudioFrame->data), pAudioFrame->nb_samples);
                    if (swr_size < 0) {
                        printf("swr_convert failed\n");
                        break;
                    }
                    dst_bufsize = av_samples_get_buffer_size(nullptr, 2, swr_size, AV_SAMPLE_FMT_S16, 1);
                    if (dst_bufsize < 0) {
                        dst_bufsize = 0;
                        break;
                    }
                    // printf("pts:%lld\t packet size:%d swr_size:%d AudioLen:%d dst_bufsize:%d\n", pkt.pts, audio->pFrame->pkt_size, swr_size, audio->AudioLen, dst_bufsize);
                }
            }

            AudioLen = dst_bufsize;
            AudioPos = (Uint8 *)AudioOutBuffer;
            while (AudioLen > 0)
                SDL_Delay(1);
            // break;
        }
        av_packet_unref(&pkt);

        Audio.packetarray[i].state = 0;
        i++;
        if (i >= PACKET_MAX_SIZE)
            i = 0;
        Audio.rindex = i;
    }
    fprintf(stdout, "thread_audio finished\n");
}

void AVState::videodecodeframe() {
    AVPacket pkt;
    int i = 0, j = 0;
    int ret = 0;

    while (1) {
        if (thread_exit)
            break;
        if (thread_pause) {
            usleep(10000);
            break;
            // continue;
        }

        if (IsPacketArrayEmpty(Video)) {
            SDL_Delay(1);
            continue;
        }

        i = Video.rindex;
        pkt = Video.packetarray[i].packet;
        if (pts != 0 && pkt.pts == pts)
        {
            // thread_exit = 1;
        }
        if (pkt.stream_index == videoindex)
        {
            while (1)
            {
                ret = avcodec_receive_frame(pVideoCodecCtx, pVideoFrame);

                if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
                {
                    fprintf(stderr, "avcodec_receive_frame faild, ret : %d\n", ret);
                    break;
                }
                else if (ret == AVERROR_EOF)
                {
                    fprintf(stderr, "AVERROR_EOR\n");
                    avcodec_flush_buffers(pVideoCodecCtx);
                }
                else if (ret == AVERROR(EAGAIN))
                {
                    break;
                }
                else
                {
                    if (fq.windex >= PACKET_MAX_SIZE)
                        fq.windex = 0;

                    if (IsFrameFull(fq))
                    {
                        usleep(5000);
                        continue;
                    }
                    printf("frame windex : %d\n", fq.windex);

                    j = fq.windex;
                    fq.frame_array[j].frame = *(pVideoFrame);
                    fq.frame_array[j].frameyuv = *(pVideoFrameYUV);
                    fq.frame_array[j].state = 1;
                    fq.windex++;
                }
            }
            ret = avcodec_send_packet(pVideoCodecCtx, &pkt);
            if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
            {
                printf("avcodec_send_packet failed, ret : %d\n", ret);
            }
            av_packet_unref(&pkt);
        }

        Video.packetarray[i].state = 0;
        i++;
        if (i >= PACKET_MAX_SIZE)
            i = 0;
        Video.rindex = i;
        // break;
    }
}

int AVState::avstate_init(std::string url) {
    int ret;

    pFormatCtx = nullptr;
    ret = avformat_open_input(&pFormatCtx, url.c_str(), nullptr, nullptr);
    if (ret < 0) {
        fprintf(stderr, "avformat_open_input failed: %s\n", av_err2str(ret));
        return ret;
    }

    ret = avformat_find_stream_info(pFormatCtx, nullptr);
    if (ret < 0) {
        fprintf(stderr, "avformat_find_stream_info failed: %s\n", av_err2str(ret));
        return ret;
    }

    videoindex = -1;
    audioindex = -1;

    audioindex = av_find_best_stream(pFormatCtx, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    videoindex = av_find_best_stream(pFormatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);

    ret = SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER);
    if (ret) {
        fprintf(stderr, "SDL_Init failed: %d\n", ret);
        return -1;
    }

    // printf info
    av_dump_format(pFormatCtx, 0, url.c_str(), 0);
    // printf info

    if (audioindex == -1) {
        fprintf(stderr, "find audio stream failed\n");
         return -1;
    } else {// 获取 audio 解码器
        pAudioCodecCtx = avcodec_alloc_context3(nullptr);
        ret = avcodec_parameters_to_context(pAudioCodecCtx, pFormatCtx->streams[audioindex]->codecpar);
        if (ret < 0) {
            fprintf(stderr, "avcodec_parameters_to_context faild: %s\n", av_err2str(ret));
            return -1;
        }

        pAudioCodec = avcodec_find_decoder(pAudioCodecCtx->codec_id);

        if (pAudioCodec == nullptr) {
            fprintf(stderr, "pCodec failed\n");
            return -1;
        }
        if (avcodec_open2(pAudioCodecCtx, pAudioCodec, nullptr) < 0) {
            printf("avcodec_open2 failed\n");
            return -1;
        }

        wanted_spec.freq = 44100;          // 每秒向音频设备发送的 sample 数据，采样率越大质量越好
        wanted_spec.format = AUDIO_S16SYS; // 本地音频字节序
        wanted_spec.channels = 2;
        wanted_spec.silence = 0;           // 设置静音值
        wanted_spec.samples = 1024;        // 音频缓冲区大小, format * channels
        wanted_spec.callback = fill_audio; // 回调函数，获取音频码流送个输出设备
        wanted_spec.userdata = nullptr;

        ret = SDL_OpenAudio(&wanted_spec, nullptr);
        if (ret < 0) {
            fprintf(stderr, "SDL_OpenAudio faild\n");
            return -1;
        }

        AuconvertCtx = swr_alloc();
        if (AuconvertCtx == nullptr) {
            fprintf(stderr, "swr_alloc failed\n");
            return -1;
        }
//        printf("duration = %lld, time = %f, av_q2d(pFormatCtx->streams[audioindex]->time_base) : %f\n", (unsigned long long)pFormatCtx->streams[audioindex]->duration, \
               pFormatCtx->streams[audioindex]->duration * av_q2d(pFormatCtx->streams[audioindex]->time_base), av_q2d(pFormatCtx->streams[audioindex]->time_base));

        /* set options */
        if (pAudioCodecCtx->channel_layout == 0)
            av_opt_set_int(AuconvertCtx, "in_channel_layout", av_get_default_channel_layout(pAudioCodecCtx->channels), 0);
        else
            av_opt_set_int(AuconvertCtx, "in_channel_layout", pAudioCodecCtx->channel_layout, 0);

        av_opt_set_int(AuconvertCtx, "in_sample_rate", pAudioCodecCtx->sample_rate, 0);
        av_opt_set_sample_fmt(AuconvertCtx, "in_sample_fmt", pAudioCodecCtx->sample_fmt, 0);

        av_opt_set_int(AuconvertCtx, "out_channel_layout", AV_CH_LAYOUT_STEREO, 0);
        av_opt_set_int(AuconvertCtx, "out_channels", 2, 0);
        av_opt_set_int(AuconvertCtx, "out_sample_rate", 44100, 0);
        av_opt_set_sample_fmt(AuconvertCtx, "out_sample_fmt", AV_SAMPLE_FMT_S16, 0);

        swr_init(AuconvertCtx);

        th1 = std::thread(&AVState::audio_thread, this);
        // 播放x
        SDL_PauseAudio(0);
    }

    if (videoindex == -1) {
        fprintf(stderr, "find video stream failed\n");
        // return -1;
    } else { // 创建 video 线程
        pVideoCodecCtx = avcodec_alloc_context3(nullptr);
        ret = avcodec_parameters_to_context(pVideoCodecCtx, pFormatCtx->streams[videoindex]->codecpar);
        if (ret < 0) {
            fprintf(stderr, "avcodec_parameters_to_context failed: %s\n", av_err2str(ret));
            return -1;
        }

        pVideoCodec = avcodec_find_decoder(pVideoCodecCtx->codec_id);
        if (nullptr == pVideoCodec) {
            fprintf(stderr, "avcodec_find_decode failed\n");
            return -1;
        }

        ret = avcodec_open2(pVideoCodecCtx, pVideoCodec, nullptr);
        if (ret < 0) {
            fprintf(stderr, "avcodec_open2 failed: %d\n", ret);
            return -1;
        }

        std::cout << "duration : " << static_cast<unsigned long long>(pFormatCtx->streams[videoindex]->duration) << ", time = " \
            << pFormatCtx->streams[videoindex]->duration * av_q2d(pFormatCtx->streams[videoindex]->time_base) << std::endl;
        pVideoFrame = av_frame_alloc();
        pVideoFrameYUV = av_frame_alloc();
        VideoOutBuffer = (unsigned char *)av_malloc(av_image_get_buffer_size(AV_PIX_FMT_YUV420P, pVideoCodecCtx->width, pVideoCodecCtx->height, 1));
        av_image_fill_arrays(pVideoFrameYUV->data, pVideoFrameYUV->linesize, VideoOutBuffer, AV_PIX_FMT_YUV420P, pVideoCodecCtx->width, pVideoCodecCtx->height, 1);


        VpconvertCtx = sws_getContext(pVideoCodecCtx->width, pVideoCodecCtx->height, pVideoCodecCtx->pix_fmt, pVideoCodecCtx->width, pVideoCodecCtx->height, AV_PIX_FMT_YUV420P, SWS_BICUBIC,
                                               nullptr, nullptr, nullptr);

        screen_w = pVideoCodecCtx->width;
        screen_h = pVideoCodecCtx->height;

        screen = SDL_CreateWindow(url.c_str(), SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, screen_w, screen_h, SDL_WINDOW_OPENGL);
        if (!screen) {
            fprintf(stderr, "SDL could not create window\n");
            return -1;
        }

        sdlrender = SDL_CreateRenderer(screen, -1, 0);
        sdltexture = SDL_CreateTexture(sdlrender, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING,
                                       pVideoCodecCtx->width, pVideoCodecCtx->height);

        sdlrect.x = 0;
        sdlrect.y = 0;
        sdlrect.w = screen_w;
        sdlrect.h = screen_h;

        dpt = std::thread(&AVState::decodepacket, this);
        srt = std::thread(&AVState::sdlrefreshthread, this);
        dft = std::thread(&AVState::videodecodeframe, this);
    }
    return 0;
}

void AVState::event_loop() {
    int i = 0;
    while (1) {
        SDL_Event event;
        SDL_WaitEvent(&event);
        if (event.type == SFM_REFRESH_EVENT) {
            if (IsFrameEmpty(fq))
                continue;

            i = fq.rindex;
            AVFrame frame = fq.frame_array[i].frame;
            AVFrame frameYUV = fq.frame_array[i].frameyuv;
            fprintf(stdout, "frame rindex %d, frame->linesize : %d\n", i, fq.frame_array[i].frame.linesize);

            sws_scale(VpconvertCtx, (const uint8_t *const *)frame.data, frame.linesize, 0, pVideoCodecCtx->height,
                      frameYUV.data, frameYUV.linesize); //视频像素格式和分辨率的转换

            SDL_UpdateTexture(sdltexture, &sdlrect, frameYUV.data[0], frameYUV.linesize[0]);
            SDL_RenderClear(sdlrender);
            SDL_RenderCopy(sdlrender, sdltexture, nullptr, &sdlrect);
            SDL_RenderPresent(sdlrender);

            fq.frame_array[i].state = 0;
            i++;
            if (i >= PACKET_MAX_SIZE)
                i = 0;
            fq.rindex = i;
        }
        else if (event.type == SDL_KEYDOWN) {
            printf("SDL_KEYDOWN\n");
            if (event.key.keysym.sym == SDLK_SPACE)
                thread_pause = !thread_pause;
        }
        else if (event.type == SDL_QUIT) {
            printf("SDL_QUIT\n");
            thread_exit = 1;
            exit(0);
        }
    }
}

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "没有检测到视频源\n");
        return -1;
    }
    std::string url = argv[1];
    std::shared_ptr<AVState> mAvStateImpl = AVState::getInstance();
    int ret = mAvStateImpl->avstate_init(url);

    mAvStateImpl->event_loop();
    return 0;
}