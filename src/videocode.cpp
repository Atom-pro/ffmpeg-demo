//
// Created by 李林超 on 2020/9/24.
//

#include "videocode.h"
#include "play.h"

char av_error[AV_ERROR_MAX_STRING_SIZE] = { 0 };

void VideoState::saveYUV420PFrame(AVFrame* frame) {
    if (fp) {
        int linesizeY = frame->linesize[0];
        int linesizeU = frame->linesize[1];
        int linesizeV = frame->linesize[2];

        uint8_t *Y = frame->data[0];
        uint8_t *U = frame->data[1];
        uint8_t *V = frame->data[2];


        for (int i = 0; i < frame->height; i++) {//1176
            fwrite(Y, 1, static_cast<size_t>(frame->width), fp);
            Y += linesizeY;
        }
        for (int i = 0; i < frame->height / 2; i++) {//3072
            fwrite(U, 1, static_cast<size_t>(frame->width / 2), fp);
            U += linesizeU;
        }
        for (int i = 0; i < frame->height / 2; i++) {//2048
            fwrite(V, 1, static_cast<size_t>(frame->width / 2), fp);
            V += linesizeV;
        }
    }
}

int VideoState::flush_encoder(AVFormatContext *format, int index) {
    int ret = 0;
    int gotframe = 0;
    AVPacket endpkt;
    if (!(format->streams[index]->codec->codec->capabilities & AV_CODEC_CAP_DELAY))
        return 0;

    while (1) {
        fprintf(stdout, "flushing  stream encoder\n");
        endpkt.data = nullptr;
        endpkt.size = 0;
        av_init_packet(&endpkt);

        ret = avcodec_encode_video2(format->streams[index]->codec, &endpkt, nullptr, &gotframe);
        av_frame_free(nullptr);
        if (ret < 0) {
            break;
        }
        if (!gotframe) {
            ret = 0;
            break;
        }
        fprintf(stdout, "flush encoder pkt.size:%d\n", endpkt.size);
        ret = av_write_frame(format, &endpkt);
        if (ret < 0) {
            break;
        }
    }
    return ret;
}

//void VideoState::getYUV420PFrame(AVFrame *frame, FILE *fp) {
//    if (fp) {
//        for ()
//    }
//}

VideoState::VideoState()
{
    videoq = std::shared_ptr<PacketQueue>(new PacketQueue);
    frameq = std::shared_ptr<FrameQueue>(new FrameQueue);
}

VideoState::~VideoState()
{
    if (pCodecCtx)
        avcodec_close(pCodecCtx);
    if (th1.joinable())
        th1.join();
    if (fp) {
        fclose(fp);
        fp = nullptr;
    }
    if (ecodecCtx)
        avcodec_close(ecodecCtx);
    if (eframe)
        av_free(eframe);
    if (epacket)
        av_packet_free(&epacket);
    if (eformatCtx->pb)
        avio_close(eformatCtx->pb);
    if (eformatCtx)
        avformat_free_context(eformatCtx);
    if (inputfile)
        fclose(inputfile);
}

enum AVCodecID encodetype(std::string argv) {
    if (argv == "h264") {
        return AV_CODEC_ID_H264;
    } else if (argv == "h265") {
        return AV_CODEC_ID_H265;
    } else if (argv == "mpeg1") {
        return AV_CODEC_ID_MPEG1VIDEO;
    } else if (argv == "mpeg2") {
        return AV_CODEC_ID_MPEG2VIDEO;
    }
    return AV_CODEC_ID_NONE;
}

int VideoState::videoencode(AVFormatContext *format, char **argv) {
    int ret = 0;
//    enum AVCodecID codec_id = AV_CODEC_ID_NONE;
//    codec_id = encodetype(argv[3]);

    eformatCtx = avformat_alloc_context();

    inputfile = fopen(argv[3], "rb");
    if (!inputfile) {
        fprintf(stderr, "fopen input file failed\n");
        return -1;
    }
    std::string outputfilepath = argv[4];
    AVOutputFormat *avOutputFormat = av_guess_format(nullptr, outputfilepath.c_str(), nullptr);
    eformatCtx->oformat = avOutputFormat;

    ret = avio_open(&eformatCtx->pb, outputfilepath.c_str(), AVIO_FLAG_WRITE);
    if (ret < 0) {
        fprintf(stderr, "avio_open outputfilepath failed: %s\n", averr2str(ret));
        return -1;
    }
    estream = avformat_new_stream(eformatCtx, nullptr); // 创建输出码流->内部创建存放的内存
    ecodecCtx = estream->codec; // 获取编码器上下文
    ecodecCtx->codec_id = avOutputFormat->video_codec; // 为编码器分配 ID
    ecodecCtx->codec_type = AVMEDIA_TYPE_VIDEO; // 设置编码器类型为视频编码器
    ecodecCtx->pix_fmt = AV_PIX_FMT_YUV420P; // 设置视频像素格式，具体根据解码时类型
    ecodecCtx->width = 720; // 设置视频尺寸
    ecodecCtx->height = 1280;
    ecodecCtx->time_base.num = 1; // 设置视频的帧率为 25FPS
    ecodecCtx->time_base.den = 25;
    ecodecCtx->bit_rate = 1000000; // 设置视频的码率，码率的计算方式：码率（Kbps）= (视频文件总大小-音频大小)Mb * 1024 * 1024 * 8 / 1000 / 视频时长
    ecodecCtx->gop_size = 250; // 设置的 GOP，GOP 会影响视频质量，GOP（画面组，一组连续的画面），IPB 帧里面关键帧的大小，越小视频越小
    ecodecCtx->qmin = 10; // 设置量化参数，量化系数越小视频越清晰，一般情况，最小默认为 10， 最大为 51
    ecodecCtx->qmax = 51;
    ecodecCtx->max_b_frames = 0; // 设置 B 帧最大值， I 帧和 P 帧之间 B 帧的数量，一般在 0-16 之间, I 帧的压缩比最低，大概是 6-7，P 帧20，B 帧 50，B 帧能节省空间

    // 查找编码器
    ecodec = avcodec_find_encoder(ecodecCtx->codec_id);  // 软件编码器 libxh264, 617M 的 YUV 文件编码时间 9.3s
//    ecodec = avcodec_find_encoder_by_name("h264_videotoolbox"); // 硬件编码器 h264_videctoolbox 617M 的 YUV 文件编码时间 1.6s
    if (!ecodec) {
        fprintf(stderr, "Could not find encodec\n");
        return -1;
    }
    fprintf(stdout, "encoder name : %s\n", ecodec->name);

    // 打开编码器, H264
    AVDictionary *param = 0;
    if (ecodecCtx->codec_id == AV_CODEC_ID_H264) {
        av_dict_set(&param, "prest", "slow", 0);  // 防止编码延迟，兼容旧版的编码时会有10帧左右的延迟
        av_dict_set(&param, "tune", "zerolatency", 0);
    }
    ret = avcodec_open2(ecodecCtx, ecodec, &param);
    if (ret < 0) {
        fprintf(stderr, "avcodec_open2 failed: %s\n", averr2str(ret));
        return -1;
    }

    // 写文件头信息
    ret = avformat_write_header(eformatCtx, nullptr);
    if (ret < 0) {
        fprintf(stderr, "avformat write file header failed: %s\n", averr2str(ret));
        return -1;
    }

    // 循环编码 YUV 文件，
    int ebuffersize = av_image_get_buffer_size(ecodecCtx->pix_fmt, ecodecCtx->width, ecodecCtx->height, 1);
    int esize = ecodecCtx->width * ecodecCtx->height; //
    uint8_t *eoutbuffer = static_cast<uint8_t*>(av_malloc(ebuffersize)); // 创建缓冲区

    // 保存帧数据
    eframe = av_frame_alloc();
    eframe->width = ecodecCtx->width;
    eframe->height = ecodecCtx->height;
    eframe->format = ecodecCtx->pix_fmt;
    av_image_fill_arrays(eframe->data, eframe->linesize, eoutbuffer, ecodecCtx->pix_fmt, ecodecCtx->width, ecodecCtx->height, 1);

    epacket = static_cast<AVPacket*>(av_malloc(ebuffersize));
    int i = 0;
    int res = 0;
    int frameindex = 1;
    struct timeval tv;
    gettimeofday(&tv, nullptr);
    uint64_t start_time_sec = tv.tv_sec;
    uint64_t start_time_usec = tv.tv_usec;
    while (1) {
//        getYUV420PFrame(eframe, inputfile);
        if (fread(eoutbuffer, 1, esize * 3 / 2, inputfile) < 0) {
            fprintf(stderr, "fread failed\n");
            break;
        } else if (feof(inputfile)) {
            break;
        }

        eframe->data[0] = eoutbuffer;
        eframe->data[1] = eoutbuffer + esize;
        eframe->data[2] = eoutbuffer + esize * 5 / 4;
        eframe->pts = i;
        i++;

        res = avcodec_send_frame(ecodecCtx, eframe);
        if (res < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
            fprintf(stdout, "avcodec_receive_packet%d\n", res);
        }
        res = avcodec_receive_packet(ecodecCtx, epacket);
        if (res == 0) {
            epacket->stream_index = estream->index;
            res = av_write_frame(eformatCtx, epacket);
            frameindex++;
            if (res < 0) {
                fprintf(stderr, "output frame failed: %s\n", averr2str(res));
                break;
            }
//            fprintf(stdout, "avcodec_receive_packet%d\n", res);
        }
    }
    flush_encoder(eformatCtx, 0);
    av_write_trailer(eformatCtx);
    gettimeofday(&tv, nullptr);
    uint64_t end_time_sec = tv.tv_sec;
    uint64_t end_time_usec = tv.tv_usec;
    uint64_t time = end_time_sec - start_time_sec;
    time += time * 1000000 + (end_time_usec - start_time_usec);
    fprintf(stdout, "编码时间: %ld 毫秒\n", time/1000);
    av_free(eoutbuffer);
}

void VideoState::Display() {
    if (videoindex != -1) {
        if (frameq->queue.empty())
            video_refresh(1);
        frameq->get(frame);

        double currentpts = *static_cast<double*>(frame->opaque);
        double del = currentpts - framelastpts;
        if (del <= 0 || del >= 1.0)
            del = framelastpts;

        framelastpts = currentpts;
        double refclock = AudioState::getInstance()->getAudioClock();
        double diff = currentpts - refclock;
        double threshold = (del > SYNC_THRESHOLD) ? del : SYNC_THRESHOLD;
        fprintf(stdout, "diff = %f, del = %f, currentpts = %f\n", diff, del, currentpts);

        if (fabs(diff) < NOSYNC_THRESHOLD) {
            if (diff < -threshold)
                del = 0;
            else if (diff >= threshold)
                del *= 2;
        }

        video_refresh(del * 1000);
        sws_scale(vpconvertctx, static_cast<const uint8_t *const *>(frame->data), frame->linesize, 0, pCodecCtx->height,
                  frameYUV->data, frameYUV->linesize); //视频像素格式和分辨率的转换

        SDL_UpdateTexture(sdltexture, &sdlrect, frameYUV->data[0], frameYUV->linesize[0]);
        SDL_RenderClear(sdlrender);
        SDL_RenderCopy(sdlrender, sdltexture, nullptr, &sdlrect);
        SDL_RenderPresent(sdlrender);
    }
}

uint32_t sdl_refresh_timer_cb(uint32_t interval, void *is) {
//    while (1) {
        SDL_Event event;
        event.type = SFM_REFRESH_EVENT;
        SDL_PushEvent(&event);
//        SDL_Delay(interval);
//        break;
//    }
}

int VideoState::video_refresh(int delay) {
    fprintf(stdout, "delay: %d\n", delay);
    std::lock_guard<std::mutex> lock(syncmutex);
    SDL_AddTimer(delay, sdl_refresh_timer_cb, nullptr);
}

double VideoState::synchronize(AVFrame *frame, double pts) {
    double frame_delay;
    if (pts != 0)
        videoclock = pts;
    else
        pts = videoclock;

    framedelay = av_q2d(stream->codec->time_base);
    framedelay += frame->repeat_pict * (framedelay * 0.5);

    videoclock += framedelay;
    return pts;
}

void VideoState::videodecoder() {
    double pts;
    int ret = 0;
    while (1) {
        videoq->get(&packet);
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
            else if (ret >= 0){
                if ((pts = av_frame_get_best_effort_timestamp(frame)) == AV_NOPTS_VALUE)
                    pts = 0;
                pts *= av_q2d(stream->time_base);
                pts = synchronize(frame, pts);
                frame->opaque = &pts;
                if (frameq->nb_frames >= frameq->FrameQueueSize)
                    SDL_Delay(10);
                frameq->put(frame);
#ifdef PCM
                saveYUV420PFrame(frame);
#endif
            }
        }
        ret = avcodec_send_packet(pCodecCtx, &packet);
        if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF) {
            fprintf(stderr, "avcodec_send_packet failed, ret : %d\n", ret);
        }
        av_free_packet(&packet);
    }
}

int VideoState::videodecode(AVFormatContext *format, int videoindex, char* url, std::string output) {
    int ret = 0;
    pFormatCtx = format;
    videoindex = videoindex;
    stream = pFormatCtx->streams[videoindex];
    pCodecCtx = avcodec_alloc_context3(nullptr);
    ret = avcodec_parameters_to_context(pCodecCtx, stream->codecpar);
    if (ret < 0) {
        fprintf(stderr, "avcodec_parameters_to_context failed: %s\n", averr2str(ret));
        return -1;
    }
#ifdef PCM
    fp = fopen(output.c_str(), "wb");
    if (fp == nullptr) {
        fprintf(stderr, "fopen output failed\n");
        return -1;
    }
#endif
    pCodec = avcodec_find_decoder(pCodecCtx->codec_id);
    if (nullptr == pCodec) {
        fprintf(stderr, "avcodec_find_decode failed\n");
        return -1;
    }

    ret = avcodec_open2(pCodecCtx, pCodec, nullptr);
    if (ret < 0) {
        fprintf(stderr, "avcodec_open2 failed: %s\n", averr2str(ret));
        return -1;
    }

    fprintf(stdout, "video duration: %d\n", stream->duration/1000);
    frame = av_frame_alloc();
    frameYUV = av_frame_alloc();
    outbuffer = (unsigned char *)av_malloc(av_image_get_buffer_size(AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height, 1));
    av_image_fill_arrays(frameYUV->data, frameYUV->linesize, outbuffer,AV_PIX_FMT_YUV420P, pCodecCtx->width, pCodecCtx->height, 1);

    vpconvertctx = sws_getContext(pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt, pCodecCtx->width, pCodecCtx->height,
            AV_PIX_FMT_YUV420P, SWS_BICUBIC, nullptr, nullptr, nullptr);

    screen_w = pCodecCtx->width;
    screen_h = pCodecCtx->height;

    screen = SDL_CreateWindow(url, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                     screen_w, screen_h, SDL_WINDOW_OPENGL);

    if (!screen) {
        fprintf(stdout, "SDL could not create window\n");
        return -1;
    }
    sdlrender = SDL_CreateRenderer(screen, -1, 0);
    sdltexture = SDL_CreateTexture(sdlrender, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING,
                                          pCodecCtx->width, pCodecCtx->height);

    sdlrect.x = 0;
    sdlrect.y = 0;
    sdlrect.w = screen_w;
    sdlrect.h = screen_h;
    th1 = std::thread(&VideoState::videodecoder, this);
    video_refresh(40);
}

std::shared_ptr<VideoState> VideoState::getInstance() {
    std::mutex mutex;
    std::lock_guard<std::mutex> lock(mutex);
    std::shared_ptr<VideoState> mInstance = std::shared_ptr<VideoState>(new VideoState);
    if (!mInstance)
        return nullptr;
    return mInstance;
}