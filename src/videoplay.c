#include "videoplay.h"

struct Videoplay *vp;

int thread_exit = 0;
int thread_pause = 0;
int inde = 0;

int sdl_refresh_thread(void *opaque)
{
    thread_exit = 0;
    thread_pause = 0;
    while (!thread_exit)
    {
        if (!thread_pause)
        {
            SDL_Event event;
            event.type = SFM_REFRESH_EVENT;
            SDL_PushEvent(&event);
        }
        SDL_Delay(40);
    }
    thread_exit = 0;
    thread_pause = 0;

    SDL_Event event;
    event.type = SFM_BREAK_EVENT;
    SDL_PushEvent(&event);
    printf("finish sdl thread\n");
    return 0;
}

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        printf("没有检测到视频源");
        return -1;
    }

    vp = (Videoplay *)malloc(sizeof(Videoplay));

    char *url = argv[1];
    av_register_all();
    avformat_network_init();
    vp->pFormatCtx = avformat_alloc_context();

    if (avformat_open_input(&vp->pFormatCtx, url, NULL, NULL) < 0)
    {
        printf("avformat_open_input open faild\n");
        return -1;
    }

    if (avformat_find_stream_info(vp->pFormatCtx, NULL) < 0)
    {
        printf("avformat_find_stream_info faild\n");
        return -1;
    }

    vp->videoindex = -1;
    int i = 0;
    for (i = 0; i < vp->pFormatCtx->nb_streams; i++)
    {
        if (vp->pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            vp->videoindex = i;
            break;
        }
    }
    if (vp->videoindex == -1)
    {
        printf("find stream videoindex faild\n");
        return -1;
    }
    printf("video->duration = %lld, time = %f\n", (unsigned long long)vp->pFormatCtx->streams[vp->videoindex]->duration, vp->pFormatCtx->streams[vp->videoindex]->duration * av_q2d(vp->pFormatCtx->streams[vp->videoindex]->time_base));

    vp->pCodecCtx = vp->pFormatCtx->streams[vp->videoindex]->codec;
    vp->pCodec = avcodec_find_decoder(vp->pCodecCtx->codec_id);
    if (NULL == vp->pCodec)
    {
        printf("avcodec_find_decode faild\n");
        return -1;
    }

    if (avcodec_open2(vp->pCodecCtx, vp->pCodec, NULL) < 0)
    {
        printf("avcodec_open2 faild\n");
        return -1;
    }

    vp->pFrame = av_frame_alloc();
    vp->pFrameYUV = av_frame_alloc();
    vp->out_buffer = (unsigned char *)av_malloc(av_image_get_buffer_size(AV_PIX_FMT_YUV420P, vp->pCodecCtx->width, vp->pCodecCtx->height, 1));
    av_image_fill_arrays(vp->pFrameYUV->data, vp->pFrameYUV->linesize, vp->out_buffer,
                         AV_PIX_FMT_YUV420P, vp->pCodecCtx->width, vp->pCodecCtx->height, 1);

    vp->packet = (AVPacket *)av_malloc(sizeof(AVPacket));

    av_dump_format(vp->pFormatCtx, 0, url, 0);

    vp->img_convert_ctx = sws_getContext(vp->pCodecCtx->width, vp->pCodecCtx->height, vp->pCodecCtx->pix_fmt,
                                         vp->pCodecCtx->width, vp->pCodecCtx->height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER))
    {
        printf("SDL_INIT faild\n");
        return -1;
    }

    vp->screen_w = vp->pCodecCtx->width;
    vp->screen_h = vp->pCodecCtx->height;

    vp->screen = SDL_CreateWindow(url, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                                  vp->screen_w, vp->screen_h, SDL_WINDOW_OPENGL);

    if (!vp->screen)
    {
        printf("SDL could not create window");
        return -1;
    }
    vp->sdlrender = SDL_CreateRenderer(vp->screen, -1, 0);
    vp->sdltexture = SDL_CreateTexture(vp->sdlrender, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING,
                                       vp->pCodecCtx->width, vp->pCodecCtx->height);

    vp->sdlrect.x = 0;
    vp->sdlrect.y = 0;
    vp->sdlrect.w = vp->screen_w;
    vp->sdlrect.h = vp->screen_h;

    vp->thread = SDL_CreateThread(sdl_refresh_thread, NULL, NULL);
    int ret = 0;

    while (1)
    {
        SDL_WaitEvent(&vp->event);
        if (vp->event.type == SFM_REFRESH_EVENT)
        {
            while (1)
            {
                if (av_read_frame(vp->pFormatCtx, vp->packet) < 0)
                    thread_exit = 1;

                if (vp->packet->stream_index == vp->videoindex)
                    break;
            }
            // ret = avcodec_decode_video2(vp->pCodecCtx, vp->pFrame, &vp->got_picture, vp->packet);
            while (1)
            {
                ret = avcodec_receive_frame(vp->pCodecCtx, vp->pFrame);
                printf("thread_video video->Video.packetarray[%d].pts = pkt.pts : %lld, time : %f\n", i, (unsigned long long)vp->packet->pts, vp->packet->pts * av_q2d(vp->pFormatCtx->streams[vp->videoindex]->time_base));

                if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
                {
                    printf("avcodec_receive_frame faild, ret : %d\n", ret);
                    break;
                }
                else if (ret == AVERROR_EOF)
                {
                    printf("AVERROR_EOR\n");
                    avcodec_flush_buffers(vp->pCodecCtx);
                }
                else if (ret == AVERROR(EAGAIN))
                {
                    break;
                }
                else
                {
                    sws_scale(vp->img_convert_ctx, (const uint8_t *const *)vp->pFrame->data, vp->pFrame->linesize, 0, vp->pCodecCtx->height,
                              vp->pFrameYUV->data, vp->pFrameYUV->linesize); //视频像素格式和分辨率的转换

                    SDL_UpdateTexture(vp->sdltexture, &vp->sdlrect, vp->pFrameYUV->data[0], vp->pFrameYUV->linesize[0]);
                    SDL_RenderClear(vp->sdlrender);
                    SDL_RenderCopy(vp->sdlrender, vp->sdltexture, NULL, &vp->sdlrect);
                    SDL_RenderPresent(vp->sdlrender);
                }
            }
            ret = avcodec_send_packet(vp->pCodecCtx, vp->packet);
            if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
            {
                printf("avcodec_send_packet failed, ret : %d\n", ret);
            }
            av_free_packet(vp->packet);
        }
        else if (vp->event.type == SDL_KEYDOWN)
        {
            printf("vp->event.type.keysym.syn = %d  SDLK_RIGHT = %d\n", vp->event.key.keysym.sym, SDLK_RIGHT);
            if (vp->event.key.keysym.sym == SDLK_SPACE)
                thread_pause = !thread_pause;
        }
        else if (vp->event.type == SDL_QUIT)
        {
            printf("code quit\n");
            thread_exit = 1;
            exit(0);
        }
        else if (vp->event.type == SFM_BREAK_EVENT)
        {
            printf("SFM_BREAK_EVENT\n");
            break;
        }
    }

    SDL_WaitThread(vp->thread, NULL);
    SDL_DestroyWindow(vp->screen);
    sws_freeContext(vp->img_convert_ctx);
    SDL_Quit();
    av_frame_free(&vp->pFrameYUV);
    av_frame_free(&vp->pFrame);
    avcodec_close(vp->pCodecCtx);
    avformat_close_input(&vp->pFormatCtx);
    return 0;
}