#include <stdio.h>

#include "src/ffmpeg_header.h"

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        printf("获取不到视频信息\n");
        return -1;
    }
    AVFormatContext *pFmtContext;
    int i, videoindex;
    AVCodecContext *pCodecContext;
    AVCodec *pCodec;
    // char filepath[] = "/media/psf/Home/Desktop/1.mp4";
    char *filepath = argv[1];
    av_register_all(); // 注册组件
    avformat_network_init(); //支持网络流
    pFmtContext = avformat_alloc_context(); // 初始化 //AVFormatContext

    if (avformat_open_input(&pFmtContext, filepath, NULL, NULL) != 0){
        printf("无法打开文件\n");
        return -1;
    }

    if (avformat_find_stream_info(pFmtContext, NULL) < 0) {
        printf("无法查到流信息\n");
        return -1;
    }

    videoindex = -1;
    printf("pFmtContext->nb_streams = %d\n", pFmtContext->nb_streams);
    for (i=0; i<pFmtContext->nb_streams; i++) // 获取视频流ID
    {
        if (pFmtContext->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            videoindex = i;
            break;
        }
    }

    printf("videoindex = %d\n", videoindex);
    pCodecContext = pFmtContext->streams[videoindex]->codec;
    pCodec = avcodec_find_decoder(pCodecContext->codec_id); // 查找解码器

    if (pCodec == NULL)
    {
        printf("Codec not found\n");
        return -1;
    }
    if (avcodec_open2(pCodecContext, pCodec, NULL) < 0) // 打开解码器
    {
        printf("Could not open codec\n");
        return -1;
    }

    AVFrame *pFrame, *pFrameYUV;
    pFrame = av_frame_alloc();
    pFrameYUV = av_frame_alloc();

    uint8_t *out_buffer=(unsigned char *)av_malloc(avpicture_get_size(AV_PIX_FMT_YUV420P,  pCodecContext->width, pCodecContext->height));
    avpicture_fill((AVPicture *)pFrameYUV, out_buffer, AV_PIX_FMT_YUV420P, pCodecContext->width, pCodecContext->height);
    // avpicture_fill(pFrameYUV->data, pFrameYUV->linesize,out_buffer, AV_PIX_FMT_YUV420P,pCodecContext->width, pCodecContext->height,1);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) < 0)
    {
        printf("SDL INIT faild\n");
        return -1;
    }
    SDL_Window *screen;
    Uint32 flags = SDL_WINDOW_SHOWN;
	screen = SDL_CreateWindow("demo", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, pCodecContext->width, pCodecContext->height, flags);
	if(!screen) {  
		printf("SDL: could not set video mode - exiting\n");  
		return -1;
    }

    SDL_Texture *bmp = SDL_CreateTexture(pCodecContext->width, pCodecContext->height, SDL_TEXTUREACCESS_STREAMING, );
    SDL_Rect rect;

    int ret, got_picture;
    static struct SwsContext *img_convert_ctx;
    int y_size = pCodecContext->width * pCodecContext->height;
    AVPacket *packet=(AVPacket *)malloc(sizeof(AVPacket));//存储解码前数据包AVPacket
	av_new_packet(packet, y_size);

    printf("文件信息-----------------------------------------\n");
	av_dump_format(pFmtContext,0,filepath,0);
	printf("-------------------------------------------------\n");
	//------------------------------
	while(av_read_frame(pFmtContext, packet)>=0)//循环获取压缩数据包AVPacket
    {
        // printf("packet->stream_index = %d, videoindex = %d\n", packet->stream_index, videoindex);
		if(packet->stream_index==videoindex)
		{
			ret = avcodec_decode_video2(pCodecContext, pFrame, &got_picture, packet);//解码。输入为AVPacket，输出为AVFrame
			if(ret < 0)
			{
				printf("解码错误\n");
				return -1;
			}
			if(got_picture)
			{
				// 像素格式转换。pFrame转换为pFrameYUV。
				img_convert_ctx = sws_getContext(pCodecContext->width, pCodecContext->height, pCodecContext->pix_fmt, pCodecContext->width, pCodecContext->height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL); 
				sws_scale(img_convert_ctx, (const uint8_t* const*)pFrame->data, pFrame->linesize, 0, pCodecContext->height, pFrameYUV->data, pFrameYUV->linesize);
				sws_freeContext(img_convert_ctx);
				//------------SDL显示--------
				SDL_LockYUVOverlay(bmp);
				bmp->pixels[0]=pFrameYUV->data[0];
				bmp->pixels[2]=pFrameYUV->data[1];
				bmp->pixels[1]=pFrameYUV->data[2];     
				bmp->pitches[0]=pFrameYUV->linesize[0];
				bmp->pitches[2]=pFrameYUV->linesize[1];   
				bmp->pitches[1]=pFrameYUV->linesize[2];
				SDL_UnlockYUVOverlay(bmp); 
				rect.x = 0;    
				rect.y = 1;    
				rect.w = pCodecContext->width;    
				rect.h = pCodecContext->height;    
				SDL_DisplayYUVOverlay(bmp, &rect); 
				//延时40ms
				SDL_Delay(40);
				//------------SDL-----------
			}
            // av_free_packet(packet);
		}
		av_free_packet(packet);
	}

    printf("============\n");
    free(out_buffer);
	av_free(pFrameYUV);
	avcodec_close(pCodecContext);
    sws_freeContext(img_convert_ctx);
	avformat_close_input(&pFmtContext);
    return 0;
}