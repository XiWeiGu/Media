#include <stdio.h>
#include <stdint.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <SDL2/SDL.h>
#include <libavutil/imgutils.h>
const char* videoname = "1_bbb_sunflower_1080p_30fps_normal.mp4";

#define ENABLE_SDL_THREAD 1
//Refresh Event
#define SFM_REFRESH_EVENT  (SDL_USEREVENT + 1)
 
#define SFM_BREAK_EVENT  (SDL_USEREVENT + 2)
 
int thread_exit=0;
int thread_pause=0;
 
int sfp_refresh_thread(void *opaque){
	thread_exit=0;
	thread_pause=0;
 
	while (!thread_exit) {
		if(!thread_pause){
			SDL_Event event;
			event.type = SFM_REFRESH_EVENT;
			SDL_PushEvent(&event);
		}
		SDL_Delay(30);
	}
	thread_exit=0;
	thread_pause=0;
	//Break
	SDL_Event event;
	event.type = SFM_BREAK_EVENT;
	SDL_PushEvent(&event);
 
	return 0;
}

int main(int argc, char* argv[]) {
    AVFormatContext *fmt_ctx = NULL; 
    AVStream *video_stream = NULL;
    AVCodecContext *video_dec_ctx = NULL;
    AVCodec *video_dec = NULL;
    int video_stream_idx = -1;
    AVFrame *frame = NULL;
    AVFrame *frameYUV = NULL;
    AVPacket pack;
    int got_pic = 0;
    uint8_t *buffer;
    int buf_size;
    struct SwsContext *img_convert_ctx;

    int screen_w, screen_h;
    SDL_Window *screen = NULL;
    SDL_Renderer *renderer = NULL;
    SDL_Rect rect;
    SDL_Texture *texture = NULL;
    SDL_Thread *sdl_video_thread;
    SDL_Event sdl_event;

    // Init FFmpeg
    if (avformat_open_input(&fmt_ctx, videoname, NULL, NULL) < 0) {
        // TODO
        return -1;
    }
    if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
        // TODO
        return -1;
    }
    if ((video_stream_idx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO,
                                                -1, -1, NULL, 0) < 0)) {
                                                    //TODO
                                                    return -1;
                                                }
    video_stream = fmt_ctx->streams[video_stream_idx];
    video_dec = avcodec_find_decoder(video_stream->codecpar->codec_id);
    if (!video_dec) {
        //TODO
        return -1;
    }
    video_dec_ctx = avcodec_alloc_context3(video_dec);
    if (!video_dec_ctx) {
        //TODO
        return -1;
    }
    if (avcodec_parameters_to_context(video_dec_ctx, video_stream->codecpar) < 0) {
        //TODO
        return -1;
    }
    if (avcodec_open2(video_dec_ctx, video_dec, NULL) < 0) {
        //TODO
        return -1;
    }
    frame = av_frame_alloc();
    frameYUV = av_frame_alloc();
    av_init_packet(&pack);
    pack.data = NULL;
    pack.size = 0;
    buffer = (uint8_t *)av_malloc(av_image_get_buffer_size(AV_PIX_FMT_YUV420P, video_dec_ctx->width, video_dec_ctx->height, 1));
    if (!buffer) {
        //TODO
        return -1;
    }
    av_image_fill_arrays(frameYUV->data, frameYUV->linesize, buffer, AV_PIX_FMT_YUV420P, video_dec_ctx->width, video_dec_ctx->height, 1);
    img_convert_ctx = sws_getContext(video_dec_ctx->width, video_dec_ctx->height, video_dec_ctx->pix_fmt,
                                     video_dec_ctx->width, video_dec_ctx->height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);

    // Init SDL
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) < 0) {
        // TODO
        return -1;
    }
    screen_h = video_dec_ctx->height, screen_w = video_dec_ctx->width;
    screen = SDL_CreateWindow("FFmpeg player", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                               screen_w, screen_h, SDL_WINDOW_OPENGL);
    if (!screen) {
        //TODO
        return -1;
    }
    renderer = SDL_CreateRenderer(screen, -1, 0);
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, screen_w, screen_h);
    rect.x = rect.y = 0, rect.w = screen_w, rect.h = screen_h;

    // create thread
    sdl_video_thread = SDL_CreateThread(sfp_refresh_thread, NULL, NULL);

#if ENABLE_SDL_THREAD
    while(1) {
        SDL_WaitEvent(&sdl_event);
        if (sdl_event.type == SFM_REFRESH_EVENT) {
            while (1) {
                if(av_read_frame(fmt_ctx, &pack) < 0) {
                    thread_exit = 1;
                    printf("Failed to read a pkt\n");
                }
                if (pack.stream_index == video_stream_idx)
                    break;
            }
            if (avcodec_decode_video2(video_dec_ctx, frame, &got_pic, &pack) < 0) {
                thread_exit = 1;
                return -1;
            }
            if (got_pic) {
                sws_scale(img_convert_ctx, (const uint8_t *const*)frame->data, frame->linesize, 0, video_dec_ctx->height, frameYUV->data, frameYUV->linesize);
                //Update SDL
                SDL_UpdateYUVTexture(texture, &rect, frameYUV->data[0], frameYUV->linesize[0],
                                                     frameYUV->data[1], frameYUV->linesize[1],
                                                     frameYUV->data[2], frameYUV->linesize[2]);
                SDL_RenderClear(renderer);
                SDL_RenderCopy(renderer, texture, NULL, &rect);
                SDL_RenderPresent(renderer);
            }
            av_packet_unref(&pack);
        }
        else if (sdl_event.type = SDL_KEYDOWN) {
            //pause 
            if (sdl_event.key.keysym.sym == SDLK_SPACE)
                thread_pause = !thread_pause;
        }
    } 
#else
    while (av_read_frame(fmt_ctx, &pack) >= 0) {
        if (pack.stream_index == video_stream_idx) {
            if (avcodec_decode_video2(video_dec_ctx, frame, &got_pic, &pack) < 0) {
                //TODO
                return -1;
            }
            if (got_pic) {
                sws_scale(img_convert_ctx, (const uint8_t *const*)frame->data, frame->linesize, 0, video_dec_ctx->height, frameYUV->data, frameYUV->linesize);
                //Update SDL
                SDL_UpdateYUVTexture(texture, &rect, frameYUV->data[0], frameYUV->linesize[0],
                                                     frameYUV->data[1], frameYUV->linesize[1],
                                                     frameYUV->data[2], frameYUV->linesize[2]);
                SDL_RenderClear(renderer);
                SDL_RenderCopy(renderer, texture, NULL, &rect);
                SDL_RenderPresent(renderer);

                SDL_Delay(40);
            }
            av_packet_unref(&pack);
        }
    }
#endif
    sws_freeContext(img_convert_ctx);
    SDL_Quit();
    av_frame_free(&frame);
    av_frame_free(&frameYUV);
    avcodec_close(video_dec_ctx);
    avformat_close_input(&fmt_ctx);
    av_free(buffer);
}