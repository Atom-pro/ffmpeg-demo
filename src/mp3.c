#include <stdio.h>

#include "ffmpeg_header.h"

#define MAX_AUDIO_FRAME_SIZE 44100  // 1 second of 48khz 32bit audio

static Uint8 *audio_chunk;
static Uint32 audio_len;
static Uint8 *audio_pos;

void  fill_audio(void *udata, Uint8 *stream, int len){ 
    SDL_memset(stream, 0, len);

	// SDL_memset(stream, 0, len);
	// if (audio_len == 0)		//有数据才播放
	// 	return;
	// len = (len>audio_len ? audio_len : len);
 
	// SDL_MixAudio(stream, audio_pos, len, SDL_MIX_MAXVOLUME);
	// audio_pos += len;
	// audio_len -= len;


    
    // 解决播放时有杂音的问题，当有数据的时候进行播放
    while (len > 0)
    {
        if (audio_len == 0)
        {
            // decodeAudio(data, audio_len);
            continue;
        }
        int temp = (len > audio_len ? audio_len : len);
        SDL_MixAudio(stream, audio_pos, temp, SDL_MIX_MAXVOLUME);
        audio_pos += temp;
        audio_len -= temp;
        stream += temp;
        len -= temp;
    }
} 

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        printf("获取不到音频信息\n");
        return -1;
    }

    AVFormatContext     *pFormatCtx;
    AVCodecContext      *pCodecCtx;
    AVCodec             *pCodec;
    AVPacket            packet;
    // AVFrame             *pFrame;
    struct SwrContext   *au_convert_ctx;
    int                 got_picture;
    int                 audioIndex;

    SDL_AudioSpec       wanted_spec;
    uint8_t             *out_buffer;
    int                 index = 0;
    char *url = argv[1];

    FILE* fp = fopen("./mp3.pcm", "wb");

    // 注册各种结构体
    av_register_all();
    pFormatCtx = avformat_alloc_context();

    if (avformat_open_input(&pFormatCtx, url, NULL, NULL) != 0)
    {
        printf("avformat_open_input faild\n");
        return -1;
    }

    if (avformat_find_stream_info(pFormatCtx, NULL) < 0)
    {
        printf("avformat_find_stream_info faild\n");
        return -1;
    }

    av_dump_format(pFormatCtx, -1, url, 0);

    audioIndex = -1;
    unsigned int i = 0;
    for(i=0; i<pFormatCtx->nb_streams; i++)
    {
        if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            audioIndex = i;
            break;
        }
    }

    if (-1 == audioIndex)
    {
        printf("find stream faild\n");
        return -1;
    }

    pCodecCtx = avcodec_alloc_context3(NULL);
    if (pCodecCtx == NULL)
    {
        printf("avcodec_alloc_context3 faild\n");
        return -1;
    }

    // 将音频流信息导入到 pCodecCtx 中
    if (avcodec_parameters_to_context(pCodecCtx, pFormatCtx->streams[audioIndex]->codecpar) < 0)
    {
        printf("avcodec_parameters_to_context faild\n");
        return -1;
    }

    // 获取音频信息中的解码器
    pCodec = avcodec_find_decoder(pCodecCtx->codec_id);
    if (pCodec == NULL)
    {
        printf("avcodec_find_decoder faild\n");
        return -1;
    }

    if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0)
    {
        printf("avcodec_open2 faild\n");
        return -1;
    }

    // 音频文件输出参数设置
    uint64_t out_channel_layout = AV_CH_LAYOUT_STEREO;  // stereo 两通道   mono 一通道
    int out_nb_samples = pCodecCtx->frame_size;
    enum AVSampleFormat out_sample_fmt = AV_SAMPLE_FMT_S16;
    int out_sample_rate = pCodecCtx->sample_rate;//44100
    int out_channels = av_get_channel_layout_nb_channels(out_channel_layout);
    int out_buffer_size = av_samples_get_buffer_size(NULL, out_channels, out_nb_samples, out_sample_fmt, 1);
    printf("out_buffer_size = %d  out_nb_samples = %d out_sample_rate = %d\n", out_buffer_size, out_nb_samples, out_sample_rate);

    out_buffer = (uint8_t *)av_malloc(MAX_AUDIO_FRAME_SIZE * 2);
    AVFrame *pFrame = av_frame_alloc();
    // AVFrame *pFrame = avcodec_alloc_frame();

    // 初始化 SDL
    if (SDL_Init(SDL_INIT_AUDIO))
    {
        printf("SDL init failed,  ErrorInfo = %s\n", SDL_GetError());
        return -1;
    }

    // 封装 SDL_AudioSpec 结构体
    wanted_spec.freq = out_sample_rate;  // 每秒向音频设备发送的 sample 数据，采样率越大质量越好
    wanted_spec.format = AUDIO_S16SYS;   // 本地音频字节序
    wanted_spec.channels = out_channels;
    wanted_spec.silence = 0;             // 设置静音值
    wanted_spec.samples = 1024; // 音频缓冲区大小, format * channels
    wanted_spec.callback = fill_audio;   // 回调函数，获取音频码流送个输出设备
    wanted_spec.userdata = pCodecCtx;    


    if (SDL_OpenAudio(&wanted_spec, NULL) < 0) 
    {
        printf("SDL_OpenAudio faild\n");
        return -1;
    }

    // 音频重采样
    au_convert_ctx = swr_alloc();
    if (au_convert_ctx == NULL)
    {
        printf("swr_alloc failed\n");
        return -1;
    }
    
    /* set options */
    av_opt_set_int(au_convert_ctx, "in_channel_layout",    pCodecCtx->channel_layout, 0);
    av_opt_set_int(au_convert_ctx, "in_sample_rate",       pCodecCtx->sample_rate, 0);
    av_opt_set_sample_fmt(au_convert_ctx, "in_sample_fmt", pCodecCtx->sample_fmt, 0);

    av_opt_set_int(au_convert_ctx, "out_channel_layout",    out_channel_layout, 0);
    av_opt_set_int(au_convert_ctx, "out_sample_rate",       out_sample_rate, 0);
    av_opt_set_sample_fmt(au_convert_ctx, "out_sample_fmt", out_sample_fmt, 0);

    // au_convert_ctx = swr_alloc_set_opts(au_convert_ctx, out_channel_layout, out_sample_fmt, out_sample_rate,
    //         pCodecCtx->channel_layout, pCodecCtx->sample_fmt, pCodecCtx->sample_rate, 0, NULL);
    swr_init(au_convert_ctx);

    printf("out_sample_rate = %d, in_sample_rate = %d\n", out_sample_rate, pCodecCtx->sample_rate);
    // 播放
    SDL_PauseAudio(0);
    int ret = 0;
    int swr_size = 0;
    int dst_nb_samples;
    int dst_bufsize = 0;

    while (av_read_frame(pFormatCtx, &packet) >= 0)
    {
        if (packet.stream_index == audioIndex)
        {
            if (avcodec_decode_audio4(pCodecCtx, pFrame, &got_picture, &packet) < 0) // got_picture 如果没有帧要解码则为 0，否则非 0
            {
                printf("avcodec_decode_audio4 failed\n");
                return -1;
            }
            if (got_picture) 
            {
                dst_nb_samples = av_rescale_rnd(swr_get_delay(au_convert_ctx, pCodecCtx->sample_rate) +
                                        pCodecCtx->frame_size, out_sample_rate, pCodecCtx->sample_rate, AV_ROUND_UP);
                ret = av_samples_alloc(&out_buffer, NULL, out_channels,
                                   dst_nb_samples, out_sample_fmt, 1);
                printf("av_samples_alloc ret:%d\n", ret);
                swr_size = swr_convert(au_convert_ctx, &out_buffer, dst_nb_samples, (const uint8_t **)pFrame->data, pFrame->nb_samples);
                dst_bufsize = av_samples_get_buffer_size(NULL, out_channels,
                                                 swr_size, out_sample_fmt, 1);
            }

            while (audio_len > 0)
            { 
                SDL_Delay(1);
            }       
            
            audio_len = out_buffer_size;
            audio_pos = (Uint8 *)out_buffer;
            // fwrite(out_buffer, 1, out_buffer_size, fp);
            printf("pts:%lld\t packet size:%d swr_size:%d audio_len:%d dst_bufsize:%d\n", packet.pts, packet.size, swr_size, audio_len, dst_bufsize);
        }
        av_packet_unref(&packet);
    }

    fclose(fp);
    swr_free(&au_convert_ctx);
	SDL_CloseAudio();//Close SDL
	SDL_Quit();
	av_free(out_buffer);
	avcodec_free_context(&pCodecCtx);
	avformat_close_input(&pFormatCtx);
    return 0;
}

// int decodeAudio(uint8_t *outData, int outSize) {
//     while (av_read_frame(pFormatCtx, &packet) >= 0)
//     {
//         if (packet.stream_index == audioIndex)
//         {
//             pk_size = packet.size;
//             if (ret = avcodec_decode_audio4(pCodecCtx, pFrame, &got_picture, &packet) < 0) // got_picture 如果没有帧要解码则为 0，否则非 0
//             {
//                 printf("avcodec_decode_audio4 failed\n");
//                 return -1;
//             }
//             if (got_picture) 
//             {
//                 fifo_size = swr_get_out_samples(au_convert_ctx, 0);
//                 swr_size = swr_convert(au_convert_ctx, &out_buffer, MAX_AUDIO_FRAME_SIZE, (const uint8_t **)pFrame->data, pFrame->nb_samples);

//                 index = packet.size - swr_size;
//             }

//             while (audio_len > 0)
//             { 
//                 SDL_Delay(1);
//             }       
            
//             audio_len = out_buffer_size;
//             audio_pos = (Uint8 *)out_buffer;
//             printf("index:%5d\t pts:%lld\t packet size:%d swr_size:%d audio_len:%d pFrame->nb_samples:%d\n", index, packet.pts, packet.size, swr_size, audio_len, pFrame->nb_samples);
//         }
//         av_packet_unref(&packet);
//     }
// }