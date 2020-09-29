#include "player.h"

static Uint8 *audio_chunk;
static Uint32 audio_len;
static Uint8 *audio_pos;
char *out_url;
struct Player *player;
FILE *out_pcm;
char *encode_input_pcm_decode_output_pcm = "./1.pcm";
char *encode_output_mp3 = "./2.mp3";
#define PCM 0

void fill_audio(void *opaque, uint8_t *stream, int len)
{
    struct Player *is = (struct Player *)opaque;
    int available = 0;
    int write_len = 0;
    int written_len = 0;
    SDL_memset(stream, 0, len);
    while (len > 0)
    {
        // printf("buffer_index:%d  audio_len:%d  len:%d  audio_pos:%p\n", is->buffer_index, audio_len, len, &audio_pos);
        if (is->buffer_index >= audio_len)
        {
            is->buffer_index = 0;
            int ret = decodeAudio();
            if (ret == -2) {
                printf("ret : %d\n", ret);
                break;
            }
            continue;
        }

        available = audio_len - is->buffer_index;
        write_len = available > len ? len : available;

        memcpy(stream, audio_pos + is->buffer_index, write_len);
        // SDL_MixAudio(stream, audio_pos, write_len, SDL_MIX_MAXVOLUME);
#ifdef PCM
        fwrite(audio_pos + is->buffer_index, 1, write_len, out_pcm);
#endif

        stream += write_len;
        is->buffer_index += write_len;
        len -= write_len;
    }
}

static int check_sample_fmt(const AVCodec *codec, enum AVSampleFormat sample_fmt){
    const enum AVSampleFormat *p = codec->sample_fmts;
    while (*p != AV_SAMPLE_FMT_NONE) {
        if (*p == sample_fmt)
            return 1;
        p++;
    }
    return 0;
}

static int select_channel_layout(const AVCodec *codec)
{
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

static int select_sample_rate(const AVCodec *codec)
{
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

void encode(AVCodecContext *codecCtx, AVFrame *frame, AVPacket *pkt, FILE *fp) {
    int ret;
    ret = avcodec_send_frame(codecCtx, frame);
    if (ret < 0) {
        fprintf(stderr, "avcodec_send_frame failed: %s\n", av_err2str(ret));
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

void audio_encode() {
    uint8_t **src_data;
    uint8_t **dst_data;
    int src_linesize, dst_linesize;
    int src_nb_channels, dst_nb_channels;
    int src_nb_samples = 1152, dst_nb_samples, max_dst_nb_samples;
    AVCodecContext *codecCtx;
    AVCodec *codec;
    codec = avcodec_find_encoder(AV_CODEC_ID_MP3);
    if (codec == NULL) {
        printf("avcodec_find_encoder failed\n");
        return;
    }
    codecCtx = avcodec_alloc_context3(codec);
    if (codecCtx == NULL) {
        printf("avcodec_alloc_context3 failed\n");
        return;
    }
    codecCtx->bit_rate = 64000;
    codecCtx->sample_fmt = AV_SAMPLE_FMT_S16P;
    if (!check_sample_fmt(codec, codecCtx->sample_fmt)) {
        printf(stderr, "encode not support sample format: %s\m", av_get_sample_fmt_name(codecCtx->sample_fmt));
        return;
    }
    codecCtx->sample_rate = select_sample_rate(codec);
    codecCtx->channel_layout = select_channel_layout(codec);
    codecCtx->channels = av_get_channel_layout_nb_channels(codecCtx->channel_layout);

    int ret = avcodec_open2(codecCtx, codec, NULL); //分配内存，调用 init, 编解码监察，编码格式是否符合要求
    if (ret < 0) {
        fprintf(stderr, "avcodec_open2 failed: %s\n", av_err2str(ret));
        return;
    }
    FILE* mp3file = fopen(out_url, "ab");
    if (!mp3file) {
        fprintf(stderr, "fopen failed\n");
        return;
    }

    AVPacket *pkt = av_packet_alloc();
    if (!pkt) {
        fprintf(stderr, "av_packet_alloc failed\n");
        return;
    }

    // 初始化存放 PCM 数据的 frame
    AVFrame *frame = av_frame_alloc();
    if (!frame) {
        fprintf(stderr, "av_frame_alloc failed\n");
        return;
    }

    frame->nb_samples = codecCtx->frame_size;
    frame->format = codecCtx->sample_fmt;
    frame->channel_layout = codecCtx->channel_layout;

    ret = av_frame_get_buffer(frame, 0);
    if (ret < 0) {
        fprintf(stderr, "av_frame_get_buffer: %s\n", av_err2str(ret));
        return;
    }

    // 重采样设置
    struct SwrContext *swr_ctx;
    swr_ctx = swr_alloc();
    if (!swr_ctx) {
        fprintf (stderr, "swr_alloc failed\n");
        goto end;
    }
    av_opt_set_int(swr_ctx, "in_channel_layout", AV_CH_LAYOUT_STEREO, 0);
    av_opt_set_int(swr_ctx, "in_sample_rate", 44100, 0);
    av_opt_set_int(swr_ctx, "in_sample_fmt", AV_SAMPLE_FMT_S16, 0);

    av_opt_set_int(swr_ctx, "out_channel_layout", AV_CH_LAYOUT_STEREO, 0);
    av_opt_set_int(swr_ctx, "out_sample_rate", 44100, 0);
    av_opt_set_int(swr_ctx, "out_sample_fmt", AV_SAMPLE_FMT_S16P, 0);

    ret = swr_init(swr_ctx);
    if (ret < 0) {
        fprintf(stderr, "swr_init failed\n");
        goto end;
    }
    src_nb_channels = av_get_channel_layout_nb_channels(AV_CH_LAYOUT_STEREO);
    // 分配样本空间  buff 大小计算方式： nb_samples（采样点数）* sample_fmt（2字节）* nb_channels 2 * 2 * 1152 = 4608
    ret = av_samples_alloc_array_and_samples(&src_data, &src_linesize, src_nb_channels, src_nb_samples, AV_SAMPLE_FMT_S16, 0);
    if (ret < 0) {
        fprintf(stderr, "av_samples_alloc_array_and_samples failed: %s\n", av_err2str(ret));
        goto end;
    }
    max_dst_nb_samples = dst_nb_samples = av_rescale_rnd(src_nb_samples, 44100, 44100, AV_ROUND_UP);
    dst_nb_channels = av_get_channel_layout_nb_channels(AV_CH_LAYOUT_STEREO);
    ret = av_samples_alloc_array_and_samples(&dst_data, &dst_linesize, dst_nb_channels,
                                             dst_nb_samples, AV_SAMPLE_FMT_S16P, 0);
    if (ret < 0) {
        fprintf(stderr, "av_samples_alloc_array_and_samples failed: %s\n", av_err2str(ret));
        goto end;
    }

    FILE *input = fopen(encode_input_pcm_decode_output_pcm, "rb");
    if (!input) {
        fprintf(stderr, "fopen input failed\n");
        goto end;
    }
    uint8_t *tmpdata = frame->data[0]; // 左声道数据
    uint8_t *tmpdata1 = frame->data[1]; // 右声道数据
    struct timeval tv;
    gettimeofday(&tv, NULL);
    uint64_t start_time_sec = tv.tv_sec;
    uint64_t start_time_usec = tv.tv_usec;
    while (!feof(input)){
        int readsize = fread(src_data[0], 1, src_nb_channels*src_nb_samples*sizeof(uint16_t), input); // 源数据大小
        src_nb_samples = readsize / (src_nb_channels*sizeof(uint16_t));
        if (!readsize)
            break;
        dst_nb_samples = av_rescale_rnd(swr_get_delay(swr_ctx, 44100) + src_nb_samples, 44100, 44100, AV_ROUND_UP);
        if (dst_nb_samples > max_dst_nb_samples) {
            av_freep(&dst_data[0]);
            ret = av_samples_alloc(dst_data, &dst_linesize, dst_nb_channels,
                                   dst_nb_samples, AV_SAMPLE_FMT_S16P, 1);
            if (ret < 0)
                break;
            max_dst_nb_samples = dst_nb_samples;
        }

        ret = swr_convert(swr_ctx, dst_data, dst_nb_samples, (const uint8_t **)src_data, src_nb_samples);
        if (ret < 0) {
            fprintf(stderr, "Error while converting\n");
            goto end;
        }
        frame->nb_samples = dst_nb_samples;
        codecCtx->frame_size = dst_nb_samples;
        frame->data[0] = dst_data[0];
        frame->data[1] = dst_data[1];

        encode(codecCtx, frame, pkt, mp3file);
        fprintf(stdout, "encode data size:%d\n",readsize);
    }
    encode(codecCtx, NULL, pkt, mp3file);
    frame->data[0] = tmpdata;
    frame->data[1] = tmpdata1;
    gettimeofday(&tv, NULL);
    uint64_t end_time_sec = tv.tv_sec;
    uint64_t end_time_usec = tv.tv_usec;
    uint64_t time = end_time_sec - start_time_sec;
    time += time * 1000000 + (end_time_usec - start_time_usec);
    fprintf(stdout, "编码时间: %ld 毫秒\n", time/1000);
end:
    if (src_data)
        av_freep(&src_data[0]);

    av_freep(&src_data);

    if (dst_data)
        av_freep(&dst_data[0]);
    av_freep(&dst_data);
    swr_free(&swr_ctx);
    fclose(mp3file);
    fclose(input);

    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&codecCtx);
}

// void  fill_audio(void *udata, Uint8 *stream, int len){
//     printf("len:%d  audio_len:%d\n", len, audio_len);
//     SDL_memset(stream, 0, len);

//     // 解决播放时有杂音的问题，当有数据的时候进行播放
//     while (len > 0)
//     {
//         if (audio_len <= 0)
//         {
//             decodeAudio();
//             printf("audio_len == %d\n", audio_len);
//             continue;
//             // break;
//         }

//         int temp = (len > audio_len ? audio_len : len);
//         SDL_MixAudio(stream, audio_pos, temp, SDL_MIX_MAXVOLUME);
//         audio_pos += temp;
//         audio_len -= temp;
//         stream += temp;
//         len -= temp;
//     }
// }

int decodeAudio()
{
    int ret = 0;
    int swr_size = 0;
    int dst_nb_samples;
    int dst_bufsize = 0;

//    printf("begin decodeAudio\n");
    while (1)
    {
        ret = av_read_frame(player->pFormatCtx, &(player->packet));
        if (ret < 0 && ret == AVERROR_EOF){
            printf("fclose====\n");
#ifdef PCM
            fclose(out_pcm);
#endif
        }
        if (player->packet.stream_index == player->audioIndex)
        {
            while (1)
            {
                ret = avcodec_receive_frame(player->pCodecCtx, player->pFrame);
                if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
                {
                    printf("avcodec_receive_frame faild, ret : %d\n", ret);
                    break;
                }
                else if (ret == AVERROR_EOF)
                {
                    printf("AVERROR_EOR\n");
                    avcodec_flush_buffers(player->pCodecCtx);
                }
                else if (ret == AVERROR(EAGAIN))
                {
                    break;
                }
                else
                {
                    dst_nb_samples = av_rescale_rnd(swr_get_delay(player->au_convert_ctx, player->pCodecCtx->sample_rate) +
                                                        1024,
                                                    44100, player->pCodecCtx->sample_rate, AV_ROUND_UP);
                    ret = av_samples_alloc(&(player->out_buffer), NULL, 2,
                                           dst_nb_samples, AV_SAMPLE_FMT_S16, 1);
                    swr_size = swr_convert(player->au_convert_ctx, &(player->out_buffer), dst_nb_samples, (const uint8_t **)(player->pFrame->data), player->pFrame->nb_samples);
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
//                    printf("pts:%lld\t packet size:%d swr_size:%d audio_len:%d dst_bufsize:%d\n", player->packet.pts, player->pFrame->pkt_size, swr_size, audio_len, dst_bufsize);
                }
            }

            ret = avcodec_send_packet(player->pCodecCtx, &player->packet);
//            printf ("player->packet.size = %d\n", player->packet.size);
            if (ret < 0 && ret != AVERROR(EAGAIN) && ret != AVERROR_EOF)
            {
                printf("avcodec_send_packet failed, ret : %d\n", ret);
            }
            audio_len = dst_bufsize;
            audio_pos = (Uint8 *)player->out_buffer;
            break;
        }
        av_packet_unref(&(player->packet));
    }
}

void unref_res()
{
    fclose(out_pcm);
    swr_free(&(player->au_convert_ctx));
    SDL_CloseAudio(); //Close SDL
    SDL_Quit();
    av_free(player->out_buffer);
    avcodec_free_context(&(player->pCodecCtx));
    avformat_close_input(&(player->pFormatCtx));
    free(player);
}

int main(int argc, char *argv[])
{
//    out_url = encode_output_mp3;
//    audio_encode();
#if 1
    if (argc != 2)
    {
        printf("获取不到音频信息\n");
        return -1;
    }

    player = (Player *)malloc(sizeof(Player) + 1);
    memset(player, 0, sizeof(Player));

    char *url = argv[1];

    // 注册各种结构体
    av_register_all();
    player->pFormatCtx = avformat_alloc_context();

    if (avformat_open_input(&(player->pFormatCtx), url, NULL, NULL) != 0)
    {
        printf("avformat_open_input faild\n");
        return -1;
    }

    if (avformat_find_stream_info(player->pFormatCtx, NULL) < 0)
    {
        printf("avformat_find_stream_info faild\n");
        return -1;
    }

    if (player->pFormatCtx->nb_streams <= 0)
    {
        printf("open nb_streams failed\n");
        return -1;
    }

    av_dump_format(player->pFormatCtx, -1, url, 0);

    player->audioIndex = -1;
    // unsigned int i = 0;
    // for(i=0; i<player->pFormatCtx->nb_streams; i++)
    // {
    //     if (player->pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
    //     {
    //         player->audioIndex = i;
    //         break;
    //     }
    // }
    player->audioIndex = av_find_best_stream(player->pFormatCtx, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0);

    if (-1 == player->audioIndex)
    {
        printf("find stream faild\n");
        return -1;
    }

    player->pCodecCtx = avcodec_alloc_context3(NULL);
    if (player->pCodecCtx == NULL)
    {
        printf("avcodec_alloc_context3 faild\n");
        return -1;
    }

    // 将音频流信息导入到 pCodecCtx 中
    if (avcodec_parameters_to_context(player->pCodecCtx, player->pFormatCtx->streams[player->audioIndex]->codecpar) < 0)
    {
        printf("avcodec_parameters_to_context faild\n");
        return -1;
    }

    // 获取音频信息中的解码器
    player->pCodec = avcodec_find_decoder(player->pCodecCtx->codec_id);
    if (player->pCodec == NULL)
    {
        printf("avcodec_find_decoder faild\n");
        return -1;
    }

    if (avcodec_open2(player->pCodecCtx, player->pCodec, NULL) < 0)
    {
        printf("avcodec_open2 faild\n");
        return -1;
    }
    player->pCodecCtx->pkt_timebase = player->pFormatCtx->streams[player->audioIndex]->time_base;
    player->mDuration = player->pFormatCtx->duration / 1000;
    printf("player->mDuration : %lld\n", player->mDuration);

    // 音频文件输出参数设置
    // uint64_t out_channel_layout = AV_CH_LAYOUT_STEREO;  // stereo 两通道   mono 一通道
    // int out_nb_samples = player->pCodecCtx->frame_size;
    // player->out_sample_fmt = AV_SAMPLE_FMT_S16;
    // player->out_sample_rate = player->pCodecCtx->sample_rate;//44100
    // player->out_channels = av_get_channel_layout_nb_channels(out_channel_layout);
    // player->out_buffer_size = av_samples_get_buffer_size(NULL, player->out_channels, out_nb_samples, player->out_sample_fmt, 1);
    // printf("out_buffer_size = %d  out_nb_samples = %d out_sample_rate = %d out_channels = %d\n", player->out_buffer_size, out_nb_samples, player->out_sample_rate, player->out_channels);

    // player->out_buffer = (uint8_t *)av_malloc(MAX_AUDIO_FRAME_SIZE * 2);
    player->pFrame = av_frame_alloc();
    // AVFrame *pFrame = avcodec_alloc_frame();

    // 初始化 SDL
    if (SDL_Init(SDL_INIT_AUDIO))
    {
        printf("SDL init failed,  ErrorInfo = %s\n", SDL_GetError());
        return -1;
    }

#ifdef PCM
    out_pcm = fopen(encode_input_pcm_decode_output_pcm, "wb");
    if (out_pcm == NULL)
    {
        fprintf(stderr, "open out_pcm failed\n");
        return -1;
    }
#endif PCM

    // 封装 SDL_AudioSpec 结构体
    player->wanted_spec.freq = 44100;          // 每秒向音频设备发送的 sample 数据，采样率越大质量越好
    player->wanted_spec.format = AUDIO_S16SYS; // 本地音频字节序
    player->wanted_spec.channels = 2;
    player->wanted_spec.silence = 0;           // 设置静音值
    player->wanted_spec.samples = 1024;        // 音频缓冲区大小, format * channels
    player->wanted_spec.callback = fill_audio; // 回调函数，获取音频码流送个输出设备
    player->wanted_spec.userdata = player;

    if (SDL_OpenAudio(&player->wanted_spec, NULL) < 0)
    {
        printf("SDL_OpenAudio faild\n");
        return -1;
    }

    // 音频重采样
    player->au_convert_ctx = swr_alloc();
    if (player->au_convert_ctx == NULL)
    {
        printf("swr_alloc failed\n");
        return -1;
    }
    printf("player->pCodecCtx->channel_layout:%llu player->pCodecCtx->channels:%d\n", player->pCodecCtx->channel_layout, player->pCodecCtx->channels);

    /* set options */
    if (player->pCodecCtx->channel_layout == 0)
        av_opt_set_int(player->au_convert_ctx, "in_channel_layout", av_get_default_channel_layout(player->pCodecCtx->channels), 0);
    else
        av_opt_set_int(player->au_convert_ctx, "in_channel_layout", player->pCodecCtx->channel_layout, 0);

    av_opt_set_int(player->au_convert_ctx, "in_sample_rate", player->pCodecCtx->sample_rate, 0);
    av_opt_set_sample_fmt(player->au_convert_ctx, "in_sample_fmt", player->pCodecCtx->sample_fmt, 0);

    av_opt_set_int(player->au_convert_ctx, "out_channel_layout", AV_CH_LAYOUT_STEREO, 0);
    av_opt_set_int(player->au_convert_ctx, "out_channels", 2, 0);
    av_opt_set_int(player->au_convert_ctx, "out_sample_rate", 44100, 0);
    av_opt_set_sample_fmt(player->au_convert_ctx, "out_sample_fmt", AV_SAMPLE_FMT_S16, 0);

    swr_init(player->au_convert_ctx);

    // 播放
    SDL_PauseAudio(0);

    while (1)
    {
        SDL_Event event;
        SDL_WaitEvent(&event);
        if (event.type == SDL_KEYDOWN)
        {
            if (event.key.keysym.sym == SDLK_SPACE){
                printf("==============\n");
                break;
            }
            if (event.key.keysym.sym == SDLK_RIGHT)
            {
                double cur = av_seek_frame(player->pFormatCtx, player->audioIndex, 3 * 1000, AVSEEK_FLAG_BACKWARD);
            }
        }
    }

    unref_res();
#endif
    return 0;
}
