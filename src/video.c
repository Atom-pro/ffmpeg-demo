#include "video.h"

AudioCtl *audio;
VideoCtl *video;
AVFormatContext *pFormatCtx;
int videoindex = -1;
int audioindex = -1;

int thread_exit = 0;
int thread_pause = 0;
uint64_t pts;
uint64_t start;

bool IsPacketArrayFull(streampacketArray is)
{
    int i = is.windex % PACKET_MAX_SIZE;
    if (is.packetarray[i].state != 0)
        return 1;
    return 0;
}

bool IsPacketArrayEmpty(streampacketArray is)
{
    int i = is.rindex % PACKET_MAX_SIZE;
    if (is.packetarray[i].state == 0)
        return 1;
    return 0;
}

bool IsFrameFull(framequeue is)
{
    int i = is.windex % PACKET_MAX_SIZE;
    if (is.frame_array[i].state != 0)
        return 1;
    return 0;
}

bool IsFrameEmpty(framequeue is)
{
    int i = is.rindex % PACKET_MAX_SIZE;
    if (is.frame_array[i].state == 0)
        return 1;
    return 0;
}

int sdl_refresh_thread(void *arg)
{
    while (!thread_exit)
    {
        if (!thread_pause)
        {
            // usleep(40000);
            SDL_Event event;
            event.type = SFM_REFRESH_EVENT;
            SDL_PushEvent(&event);
        }
//        getframe(clock)
    }
    printf("sdl_refresh_thread finished\n");
    return 0;
}

void *thread_audio(void *arg)
{
    int ret = 0;
    int i = 0;
    AVPacket pkt;
    int swr_size = 0;
    int dst_nb_samples;
    int dst_bufsize = 0;
    audio->pFrame = av_frame_alloc();
    while (1)
    {
        if (thread_exit)
            break;
        if (thread_pause)
        {
            usleep(10000);
            continue;
        }

        if (IsPacketArrayEmpty(audio->Audio))
        {
            SDL_Delay(1);
            continue;
        }
        i = audio->Audio.rindex;
        pkt = audio->Audio.packetarray[i].packet;
        double cur = av_gettime_relative() / 1000000.0;
        // printf("av_gettime_relative() : %f, current time : %f\n", cur, cur - start);
        // printf("thread_audio audio->Audio.packetarray[%d].pts = pkt.pts : %d\n", i, pkt.pts * av_q2d(pFormatCtx->streams[audioindex]->time_base));

        if (pkt.stream_index == audio->audioindex)
        {
            ret = avcodec_send_packet(audio->pCodecCtx, &pkt);
            if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
            {
                printf("avcodec_send_packet failed, ret : %d\n", ret);
            }
            while (1)
            {
                ret = avcodec_receive_frame(audio->pCodecCtx, audio->pFrame);
                if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
                {
                    printf("avcodec_receive_frame faild, ret : %d\n", ret);
                    break;
                }
                else if (ret == AVERROR_EOF)
                {
                    printf("AVERROR_EOR\n");
                    avcodec_flush_buffers(audio->pCodecCtx);
                }
                else if (ret == AVERROR(EAGAIN))
                {
                    break;
                }
                else
                {
                    dst_nb_samples = av_rescale_rnd(swr_get_delay(audio->au_convert_ctx, audio->pCodecCtx->sample_rate) +
                                                        1024,
                                                    44100, audio->pCodecCtx->sample_rate, AV_ROUND_UP);
                    ret = av_samples_alloc(&(audio->out_buffer), NULL, 2,
                                           dst_nb_samples, AV_SAMPLE_FMT_S16, 1);
                    swr_size = swr_convert(audio->au_convert_ctx, &(audio->out_buffer), dst_nb_samples, (const uint8_t **)(audio->pFrame->data), audio->pFrame->nb_samples);
                    if (swr_size < 0)
                    {
                        printf("swr_convert failed\n");
                        break;
                    }
                    dst_bufsize = av_samples_get_buffer_size(NULL, 2, swr_size, AV_SAMPLE_FMT_S16, 1);
                    if (dst_bufsize < 0)
                    {
                        dst_bufsize = 0;
                        break;
                    }
                    // printf("pts:%lld\t packet size:%d swr_size:%d audio_len:%d dst_bufsize:%d\n", pkt.pts, audio->pFrame->pkt_size, swr_size, audio->audio_len, dst_bufsize);
                }
            }

            audio->audio_len = dst_bufsize;
            audio->audio_pos = (Uint8 *)audio->out_buffer;
            while (audio->audio_len > 0)
                SDL_Delay(1);
            // break;
        }
        av_packet_unref(&pkt);

        audio->Audio.packetarray[i].state = 0;
        i++;
        if (i >= PACKET_MAX_SIZE)
            i = 0;
        audio->Audio.rindex = i;
    }
    printf("thread_audio finished\n");
}

void fill_audio(void *opaque, uint8_t *stream, int len)
{
    SDL_memset(stream, 0, len);
    while (len > 0)
    {
        if (thread_exit)
            break;
        if (thread_pause)
            continue;
        if (audio->audio_len == 0)
        {
            continue;
        }
        int temp = (len > audio->audio_len ? audio->audio_len : len);
        SDL_MixAudio(stream, audio->audio_pos, temp, SDL_MIX_MAXVOLUME);
        audio->audio_pos += temp;
        audio->audio_len -= temp;
        stream += temp;
        len -= temp;

//        clock=
    }
}

void *decode_frame(void *arg)
{
    AVPacket pkt;
    int i = 0;
    start = av_gettime_relative() / 1000000;

    while (1)
    {
        if (thread_exit)
        {
            printf("thread_exit\n");
            break;
        }
        if (av_read_frame(pFormatCtx, &pkt) < 0)
        {
             printf("av_read_frame failed, pkt.pts = %lld\n", (unsigned long long)pkt.pts);
            // thread_exit = 1;
            // SDL_Event event;
            // event.type = SFM_BREAK_EVENT;
            // SDL_PushEvent(&event);
            break;
        }

        pts = pkt.pts;
        if (pkt.stream_index == videoindex)
        {
            if (video->Video.windex >= PACKET_MAX_SIZE)
            {
                video->Video.windex = 0;
            }
            while (IsPacketArrayFull(video->Video))
            {
                continue;
                // usleep(5000);
            }

            i = video->Video.windex;
            video->Video.packetarray[i].packet = pkt;
            video->Video.packetarray[i].dts = pkt.dts;
            video->Video.packetarray[i].pts = pkt.pts;
            video->Video.packetarray[i].state = 1;
            video->Video.windex++;
            // printf("IsPacketArrayFull, i : %d, video->Video.packetarray[i].pts : %d\n", i, video->Video.packetarray[i].pts);
        }
        if (pkt.stream_index == audioindex)
        {
            if (audio->Audio.windex >= PACKET_MAX_SIZE)
                audio->Audio.windex = 0;
            while (IsPacketArrayFull(audio->Audio))
            {
                // usleep(5000);
                continue;
            }
            i = audio->Audio.windex;
            audio->Audio.packetarray[i].packet = pkt;
            audio->Audio.packetarray[i].dts = pkt.dts;
            audio->Audio.packetarray[i].pts = pkt.pts;
            audio->Audio.packetarray[i].state = 1;
            audio->Audio.windex++;
//             printf("IsPacketArrayFull, i : %d, audio->Audio.packetarray[i].pts : %d\n", i, audio->Audio.packetarray[i].pts);
        }
    }
    printf("decode_frame thread finished\n");
}

// double synchronize_video(AVFrame *frame, double pts)
// {
//     double delay;
//     // printf("frame->repeat_pict : %f, pts : %f\n", frame->repeat_pict, pts);
//     delay = av_q2d(pFormatCtx->streams[videoindex]->time_base);
//     delay += frame->repeat_pict * (delay * 0.5);
//     video->video_pts += delay;
//     printf("video->video_pts : %f\n", video->video_pts);
//     return pts;
// }

void *video_decode_frame(void *arg)
{
    AVPacket pkt;
    int i = 0, j = 0;
    int ret = 0;

    while (1)
    {
        if (thread_exit)
            break;
        if (thread_pause)
        {
            usleep(10000);
            break;
            // continue;
        }

        if (IsPacketArrayEmpty(video->Video))
        {
            SDL_Delay(1);
            continue;
        }

        i = video->Video.rindex;
        pkt = video->Video.packetarray[i].packet;
        if (pts != 0 && pkt.pts == pts)
        {
            // thread_exit = 1;
        }
        if (pkt.stream_index == video->videoindex)
        {
            ret = avcodec_send_packet(video->pCodecCtx, &pkt);
            if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
            {
                printf("avcodec_send_packet failed, ret : %d\n", ret);
            }
            while (1)
            {
                ret = avcodec_receive_frame(video->pCodecCtx, video->pFrame);

                if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
                {
                    printf("avcodec_receive_frame faild, ret : %d\n", ret);
                    break;
                }
                else if (ret == AVERROR_EOF)
                {
                    printf("AVERROR_EOR\n");
                    avcodec_flush_buffers(video->pCodecCtx);
                }
                else if (ret == AVERROR(EAGAIN))
                {
                    break;
                }
                else
                {
                    if (video->fq.windex >= PACKET_MAX_SIZE)
                        video->fq.windex = 0;

                    if (IsFrameFull(video->fq))
                    {
                        // usleep(10000);
                        continue;
                    }
                    printf("frame windex : %d\n", video->fq.windex);

                    j = video->fq.windex;
                    video->fq.frame_array[j].frame = *(video->pFrame);
                    video->fq.frame_array[j].frameyuv = *(video->pFrameYUV);
                    video->fq.frame_array[j].state = 1;
                    video->fq.windex++;
                }
            }


            av_packet_unref(&pkt);
        }

        video->Video.packetarray[i].state = 0;
        i++;
        if (i >= PACKET_MAX_SIZE)
            i = 0;
        video->Video.rindex = i;
        // break;
    }
}

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        printf("没有检测到视频源\n");
        return -1;
    }
    char *url = argv[1];
    audio = (AudioCtl *)malloc(sizeof(AudioCtl));
    video = (VideoCtl *)malloc(sizeof(VideoCtl));

    AVCodecContext *pCodecCtx;
    AVCodec *pVideoCodec, *pAudioCodec;
    AVPacket *packet;
    int ret = 0;
    pthread_t audio_thread, video_thread, video_frame;
//    sem_init(&video->video_refresh, 0, 0);

    pFormatCtx = avformat_alloc_context();

    ret = avformat_open_input(&pFormatCtx, url, NULL, NULL);
    if (ret < 0) {
        fprintf(stderr, "avformat_open_input failed: %s\n", av_err2str(ret));
        return ret;
    }

    ret = avformat_find_stream_info(pFormatCtx, NULL);
    if (ret < 0) {
        fprintf(stderr, "avformat_find_stream_info failed: %s\n", av_err2str(ret));
        return ret;
    }


    videoindex = -1;
    audioindex = -1;

    audioindex = av_find_best_stream(pFormatCtx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);
    videoindex = av_find_best_stream(pFormatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    audio->audioindex = audioindex;
    video->videoindex = videoindex;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER))
    {
        printf("SDL_Init failed\n");
        return -1;
    }
    av_dump_format(pFormatCtx, 0, url, 0);

    if (audioindex == -1)
    {
        printf("find audio stream failed\n");
        // return -1;
    }
    else // 获取 audio 解码器
    {
        audio->pCodecCtx = avcodec_alloc_context3(NULL);
        if (avcodec_parameters_to_context(audio->pCodecCtx, pFormatCtx->streams[audioindex]->codecpar) < 0)
        {
            printf("avcodec_parameters_to_context faild\n");
            return -1;
        }

        audio->pCodec = avcodec_find_decoder(audio->pCodecCtx->codec_id);

        if (audio->pCodec == NULL)
        {
            printf("audio->pCodec failed\n");
            return -1;
        }
        if (avcodec_open2(audio->pCodecCtx, audio->pCodec, NULL) < 0)
        {
            printf("avcodec_open2 failed\n");
            return -1;
        }

        audio->wanted_spec.freq = 44100;          // 每秒向音频设备发送的 sample 数据，采样率越大质量越好
        audio->wanted_spec.format = AUDIO_S16SYS; // 本地音频字节序
        audio->wanted_spec.channels = 2;
        audio->wanted_spec.silence = 0;           // 设置静音值
        audio->wanted_spec.samples = 1024;        // 音频缓冲区大小, format * channels
        audio->wanted_spec.callback = fill_audio; // 回调函数，获取音频码流送个输出设备
        audio->wanted_spec.userdata = audio;

        if (SDL_OpenAudio(&audio->wanted_spec, NULL) < 0)
        {
            printf("SDL_OpenAudio faild\n");
            return -1;
        }

        audio->au_convert_ctx = swr_alloc();
        if (audio->au_convert_ctx == NULL)
        {
            printf("swr_alloc failed\n");
            return -1;
        }
        printf("audio->duration = %lld, time = %f, av_q2d(pFormatCtx->streams[audioindex]->time_base) : %f\n", (unsigned long long)pFormatCtx->streams[audioindex]->duration,
               pFormatCtx->streams[audioindex]->duration * av_q2d(pFormatCtx->streams[audioindex]->time_base), av_q2d(pFormatCtx->streams[audioindex]->time_base));

        /* set options */
        if (audio->pCodecCtx->channel_layout == 0)
            av_opt_set_int(audio->au_convert_ctx, "in_channel_layout", av_get_default_channel_layout(audio->pCodecCtx->channels), 0);
        else
            av_opt_set_int(audio->au_convert_ctx, "in_channel_layout", audio->pCodecCtx->channel_layout, 0);

        av_opt_set_int(audio->au_convert_ctx, "in_sample_rate", audio->pCodecCtx->sample_rate, 0);
        av_opt_set_sample_fmt(audio->au_convert_ctx, "in_sample_fmt", audio->pCodecCtx->sample_fmt, 0);

        av_opt_set_int(audio->au_convert_ctx, "out_channel_layout", AV_CH_LAYOUT_STEREO, 0);
        av_opt_set_int(audio->au_convert_ctx, "out_channels", 2, 0);
        av_opt_set_int(audio->au_convert_ctx, "out_sample_rate", 44100, 0);
        av_opt_set_sample_fmt(audio->au_convert_ctx, "out_sample_fmt", AV_SAMPLE_FMT_S16, 0);

        swr_init(audio->au_convert_ctx);

        ret = pthread_create(&audio_thread, NULL, thread_audio, audio);
        if (ret < 0)
        {
            printf("thread_audio create failed\n");
            return -1;
        }
        // 播放
        SDL_PauseAudio(0);
    }

    if (videoindex == -1)
    {
        printf("find video stream failed\n");
        // return -1;
    }
    else // 创建 video 线程
    {
        video->pCodecCtx = avcodec_alloc_context3(NULL);
        if (avcodec_parameters_to_context(video->pCodecCtx, pFormatCtx->streams[videoindex]->codecpar) < 0)
        {
            printf("avcodec_parameters_to_context faild\n");
            return -1;
        }

        video->pCodec = avcodec_find_decoder(video->pCodecCtx->codec_id);
        if (NULL == video->pCodec)
        {
            printf("avcodec_find_decode faild\n");
            return -1;
        }

        if (avcodec_open2(video->pCodecCtx, video->pCodec, NULL) < 0)
        {
            printf("avcodec_open2 faild\n");
            return -1;
        }

        if (pFormatCtx->streams[videoindex]->avg_frame_rate.num == 0 || pFormatCtx->streams[videoindex]->avg_frame_rate.den == 0)
        {
            video->refresh_time = 40000;
        }
        else
        {
            video->refresh_time = 1000000 * pFormatCtx->streams[videoindex]->avg_frame_rate.den;
            video->refresh_time /= pFormatCtx->streams[videoindex]->avg_frame_rate.num;
        }
        printf("video->refresh_time = %d\n", video->refresh_time);
        printf("video->duration = %lld, time = %f, av_q2d : %f\n", (unsigned long long)pFormatCtx->streams[videoindex]->duration, pFormatCtx->streams[videoindex]->duration * av_q2d(pFormatCtx->streams[videoindex]->time_base), av_q2d(pFormatCtx->streams[videoindex]->time_base));

        video->pFrame = av_frame_alloc();
        video->pFrameYUV = av_frame_alloc();
        video->out_buffer = (unsigned char *)av_malloc(av_image_get_buffer_size(AV_PIX_FMT_YUV420P, video->pCodecCtx->width, video->pCodecCtx->height, 1));
        av_image_fill_arrays(video->pFrameYUV->data, video->pFrameYUV->linesize, video->out_buffer,
                             AV_PIX_FMT_YUV420P, video->pCodecCtx->width, video->pCodecCtx->height, 1);

        video->vp_convert_ctx = sws_getContext(video->pCodecCtx->width, video->pCodecCtx->height, video->pCodecCtx->pix_fmt,
                                               video->pCodecCtx->width, video->pCodecCtx->height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);

        video->screen_w = video->pCodecCtx->width;
        video->screen_h = video->pCodecCtx->height;

        video->screen = SDL_CreateWindow(url, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                         video->screen_w, video->screen_h, SDL_WINDOW_OPENGL);

        if (!video->screen)
        {
            printf("SDL could not create window");
            return -1;
        }
        video->sdlrender = SDL_CreateRenderer(video->screen, -1, 0);
        video->sdltexture = SDL_CreateTexture(video->sdlrender, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING,
                                              video->pCodecCtx->width, video->pCodecCtx->height);

        video->sdlrect.x = 0;
        video->sdlrect.y = 0;
        video->sdlrect.w = video->screen_w;
        video->sdlrect.h = video->screen_h;

        video->thread = SDL_CreateThread(sdl_refresh_thread, NULL, NULL);
        ret = pthread_create(&video_thread, NULL, decode_frame, video);
        if (ret)
        {
            printf("video thread create failed\n");
            return -1;
        }
        ret = pthread_create(&video_frame, NULL, video_decode_frame, video);
        if (ret)
        {
            printf("video_decode_frame thread create failed\n");
            return -1;
        }
    }

    SDL_Thread *event_tid;
    // event_tid = SDL_CreateThread(SDL_event_thread, NULL, NULL);
    int i = 0;
    uint64_t diff = 0;
    video->pFrame = av_frame_alloc();
    uint64_t delay = 0;
    while (1)
    {
        SDL_Event event;
        SDL_WaitEvent(&event);
        if (event.type == SFM_REFRESH_EVENT)
        {
            if (IsFrameEmpty(video->fq))
            {
                continue;
            }

            i = video->fq.rindex;

            AVFrame frame = video->fq.frame_array[i].frame;
            printf("frame rindex %d, frame->linesize : %d\n", i, video->fq.frame_array[i].frame.linesize);

            // AVFrame *frameyuv = video->fq.frame_array[i].frameyuv;
            sws_scale(video->vp_convert_ctx, (const uint8_t *const *)frame.data, frame.linesize, 0, video->pCodecCtx->height,
                      video->pFrameYUV->data, video->pFrameYUV->linesize); //视频像素格式和分辨率的转换

            SDL_UpdateTexture(video->sdltexture, &video->sdlrect, video->pFrameYUV->data[0], video->pFrameYUV->linesize[0]);
            SDL_RenderClear(video->sdlrender);
            SDL_RenderCopy(video->sdlrender, video->sdltexture, NULL, &video->sdlrect);
            SDL_RenderPresent(video->sdlrender);

            video->fq.frame_array[i].state = 0;
            i++;
            if (i >= PACKET_MAX_SIZE)
                i = 0;
            video->fq.rindex = i;
        }
        else if (event.type == SDL_KEYDOWN)
        {
            printf("SDL_KEYDOWN\n");
            if (event.key.keysym.sym == SDLK_SPACE)
            {
                thread_pause = !thread_pause;
            }
        }
        else if (event.type == SDL_QUIT)
        {
            printf("SDL_QUIT\n");
            thread_exit = 1;
            exit(0);
        }
    }
    // SDL_WaitThread(event_tid, NULL);
    SDL_WaitThread(video->thread, NULL);
    SDL_DestroyWindow(video->screen);
    pthread_join(video_thread, NULL);
    pthread_join(audio_thread, NULL);
    pthread_join(video_frame, NULL);
    SDL_CloseAudio();
    SDL_Quit();
    sws_freeContext(video->vp_convert_ctx);
    swr_free(&audio->au_convert_ctx);

    avcodec_close(pCodecCtx);
    avformat_close_input(&pFormatCtx);
}