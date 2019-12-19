#include <stdio.h>
#include <stdint.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavdevice/avdevice.h>
#include <libavutil/imgutils.h>
#include <libavutil/time.h>
#include <libavutil/audio_fifo.h>
#include <SDL2/SDL.h>

#define ENABLE_AUDIO 1
#define ENABLE_SDL 0

char* device_name_v = "video=Logitech HD Webcam C270";
char* device_name_a = "audio=麦克风 (HD Webcam C270)";
int ret;
// input params
AVFormatContext* fmt_ctx_v = NULL;
AVFormatContext* fmt_ctx_a = NULL;
AVInputFormat* ifmt = NULL;
AVCodecContext* codec_ctx_v = NULL;
AVCodecContext* codec_ctx_a = NULL;
AVCodec* codec_v = NULL;
AVCodec* codec_a = NULL; 
int video_idx = -1;
int audio_idx = -1;

// output params
char* out_path = "output.flv";
AVFormatContext* ofmt_ctx = NULL;
AVCodec* ocodec_v = NULL;
AVCodecContext* ocodec_ctx_v = NULL;
AVCodec* ocodec_a = NULL;
AVCodecContext* ocodec_ctx_a = NULL;
AVStream* video_st;
AVStream* audio_st;
AVAudioFifo* fifo = NULL;
uint8_t** converted_input_samples = NULL;

// time base
AVRational time_base_q = {1, AV_TIME_BASE};
int aud_next_pts = 0, vid_next_pts = 0;
int encode_video = 1, encode_audio = 1;
int framecnt = 0;
int nb_samples = 0;

// sws params
int64_t pckCount = 0;
uint8_t* buffer;
int buf_size;
struct SwsContext* pic_convert_ctx;
struct SwrContext* aud_convert_ctx;

// SDL
int screen_w, screen_h;
SDL_Window *screen = NULL;
SDL_Renderer *renderer = NULL;
SDL_Rect rect;
SDL_Texture *texture = NULL;

int main(int argc, char* argv[]) {
    avdevice_register_all();
    avformat_network_init();
    av_log_set_level(AV_LOG_INFO);
    ifmt = av_find_input_format("dshow");
    if (!ifmt) {
        printf("find dshow failed\n");
        return -1;
    }
#if (ENABLE_AUDIO)
    //init input audio
    if (avformat_open_input(&fmt_ctx_a, device_name_a, ifmt, NULL) < 0) {
        printf("open audio device failed.\n");
        return -1;
    }
    if (avformat_find_stream_info(fmt_ctx_a, NULL) < 0) {
        printf("find audio device info failed.\n");
        return -1;
    }
    if ((audio_idx = av_find_best_stream(fmt_ctx_a, AVMEDIA_TYPE_AUDIO, -1, -1, NULL, 0)) < 0) {
        printf("find audio stream index failed.\n");
        return -1;
    }
    if (!(codec_a = avcodec_find_decoder(fmt_ctx_a->streams[audio_idx]->codecpar->codec_id))) {
        printf("find audio decoder failed.\n");
        return -1;
    }
    codec_ctx_a = avcodec_alloc_context3(codec_a);
    if (avcodec_parameters_to_context(codec_ctx_a, fmt_ctx_a->streams[audio_idx]->codecpar) < 0) {
        printf("Failed to copy parameters to audio decoder context\n");
        return -1;
    }
    if (avcodec_open2(codec_ctx_a, codec_a, NULL) < 0) {
        printf("Failed to open audio decoder");
        return -1;
    }
 #endif
    // init input video
	AVDictionary *format_opts =  NULL;
	av_dict_set_int(&format_opts, "rtbufsize", 18432000  , 0);
    if (avformat_open_input(&fmt_ctx_v, device_name_v, ifmt, &format_opts) < 0) {
        printf("open video device failed.\n");
        return -1;
    }
    if (avformat_find_stream_info(fmt_ctx_v, NULL) < 0) {
        printf("find video device info failed.\n");
        return -1;
    }
    if ((video_idx = av_find_best_stream(fmt_ctx_v, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0)) < 0) {
        printf("find video stream index failed.\n");
        return -1;
    }
    if (!(codec_v = avcodec_find_decoder(fmt_ctx_v->streams[video_idx]->codecpar->codec_id))) {
        printf("find video decoder failed.\n");
        return -1;
    }
    codec_ctx_v = avcodec_alloc_context3(codec_v);
    if (avcodec_parameters_to_context(codec_ctx_v, fmt_ctx_v->streams[video_idx]->codecpar) < 0) {
        printf("Failed to copy parameters to video decoder context\n");
        return -1;
    }
    if (avcodec_open2(codec_ctx_v, codec_v, NULL) < 0) {
        printf("Failed to open video decoder");
        return -1;
    }

    // init video output
    if (avformat_alloc_output_context2(&ofmt_ctx, NULL, "flv", out_path) < 0) {
        printf("Failed to alloc output format.\n");
        return -1;
    }
    ocodec_v = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!ocodec_v) {
        printf("Failed to find output video ecoder(H264).\n");
        return -1;
    }
    ocodec_ctx_v = avcodec_alloc_context3(ocodec_v);
    if (!ocodec_ctx_v) {
        printf("Failed to alloc the output video encoder context.\n");
        return -1;
    }
    ocodec_ctx_v->pix_fmt = *ocodec_v->pix_fmts;
    ocodec_ctx_v->width = fmt_ctx_v->streams[video_idx]->codecpar->width;
    ocodec_ctx_v->height = fmt_ctx_v->streams[video_idx]->codecpar->height;
    ocodec_ctx_v->time_base.num = 1/*fmt_ctx_v->streams[video_idx]->time_base.num*/;
    ocodec_ctx_v->time_base.den = 25/*fmt_ctx_v->streams[video_idx]->time_base.den*/;
    ocodec_ctx_v->max_b_frames = 0;
    ocodec_ctx_v->bit_rate = 300000;
    ocodec_ctx_v->gop_size = 250;
    ocodec_ctx_v->codec_type = AVMEDIA_TYPE_VIDEO;
#if 0
    // h264 params
    ocodec_ctx_v->qmin = 10;
    ocodec_ctx_v->qmax = 51;
    ocodec_ctx_v->max_b_frames = 0;
    AVDictionary* params;
    av_dict_set(&params, "preset", "fast", 0);
    av_dict_set(&params, "tune", "zerolatency", 0);
#endif
    if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER) {
        ocodec_ctx_v->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }
    avcodec_open2(ocodec_ctx_v, ocodec_v, NULL);
    // add a new video stream to output format context
    video_st = avformat_new_stream(ofmt_ctx, ocodec_ctx_v->codec);
    if (!video_st) {
        printf("Failed to create a new video stream for output context.\n");
        return -1;
    }
    // This is very import! Copy the AVCodecContext params to stream.
    avcodec_parameters_from_context(video_st->codecpar, ocodec_ctx_v);
    // The stream time_base will modify by format time base
    //video_st->time_base.num = 1;
    //video_st->time_base.den = 25;

#if ENABLE_AUDIO
    // init audio output
    ocodec_a = avcodec_find_encoder(AV_CODEC_ID_AAC);
    if (!ocodec_a) {
        printf("Failed to find output audio encoder\n");
        return -1;
    }
    ocodec_ctx_a = avcodec_alloc_context3(ocodec_a);
    if (!ocodec_ctx_a) {
        printf("Failed to alloc the output audio encoder context");
        return -1;
    }
    ocodec_ctx_a->channels = 2;
    ocodec_ctx_a->channel_layout = av_get_default_channel_layout(2);
    ocodec_ctx_a->sample_rate = fmt_ctx_a->streams[audio_idx]->codecpar->sample_rate;
    ocodec_ctx_a->sample_fmt = ocodec_a->sample_fmts[0];
    ocodec_ctx_a->bit_rate = 32000;
    ocodec_ctx_a->time_base.num = 1;
    ocodec_ctx_a->time_base.den = ocodec_ctx_a->sample_rate;
    if (avcodec_open2(ocodec_ctx_a, ocodec_a, NULL) < 0) {
        printf("Failed to open output audio encoder");
        return -1;
    }
    audio_st = avformat_new_stream(ofmt_ctx, ocodec_a);
    if (!audio_st) {
        printf("Failed to create a audio stream for output\n");
        return -1;
    }
    audio_st->time_base.num = 1;
    audio_st->time_base.den = ocodec_ctx_a->sample_rate;
    // This is very import! Copy the AVCodecContext params to stream.
    avcodec_parameters_from_context(audio_st->codecpar, ocodec_ctx_a);
#endif

    // init the output file (local file)
    if (avio_open2(&ofmt_ctx->pb, out_path, AVIO_FLAG_WRITE, NULL, NULL) < 0) {
        printf("Failed to open the output file\n");
        return -1;
    }
    if (avformat_write_header(ofmt_ctx, NULL) < 0) {
        printf("Failed to write header file\n");
        return -1;
    }
    int got_frame;
    int got_pkt;
    AVPacket* pkt;
    AVPacket* pktEnc;
    pkt = av_packet_alloc();
    pktEnc = av_packet_alloc();
    AVFrame* frame;
    AVFrame* frameYUV;
    frame = av_frame_alloc();
    frameYUV = av_frame_alloc();
    buffer = (uint8_t*)av_malloc(av_image_get_buffer_size(ocodec_ctx_v->pix_fmt,
                                 ocodec_ctx_v->width, ocodec_ctx_v->height, 1));
    av_image_fill_arrays(frameYUV->data, frameYUV->linesize, buffer,
              ocodec_ctx_v->pix_fmt, ocodec_ctx_v->width, ocodec_ctx_v->height, 1);
    pic_convert_ctx = sws_getContext(ocodec_ctx_v->width, ocodec_ctx_v->height,
                                     codec_ctx_v->pix_fmt, ocodec_ctx_v->width,
                                     ocodec_ctx_v->height, ocodec_ctx_v->pix_fmt,
                                     SWS_BICUBIC, NULL, NULL, NULL);
    aud_convert_ctx = swr_alloc_set_opts(NULL,
                      av_get_default_channel_layout(ocodec_ctx_a->channels),
                      ocodec_ctx_a->sample_fmt, ocodec_ctx_a->sample_rate,
                      av_get_default_channel_layout(fmt_ctx_a->streams[audio_idx]->codecpar->channels),
                      codec_ctx_a->sample_fmt, codec_ctx_a->sample_rate,
                      0, NULL);
    swr_init(aud_convert_ctx);
    fifo = av_audio_fifo_alloc(ocodec_ctx_a->sample_fmt, ocodec_ctx_a->channels, 1);
    if (!(converted_input_samples = (uint8_t**)calloc(ocodec_ctx_a->channels, 
        sizeof(**converted_input_samples)))) {
            printf("Could not allocate converted input sample pointers\n");
            return -1;
    }
#if ENABLE_SDL
    // Init SDL
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) < 0) {
        // TODO
        return -1;
    }
    screen_h = ocodec_ctx_v->height, screen_w = ocodec_ctx_v->width;
    screen = SDL_CreateWindow("FFmpeg player", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                               screen_w, screen_h, SDL_WINDOW_OPENGL);
    if (!screen) {
        //TODO
        return -1;
    }
    renderer = SDL_CreateRenderer(screen, -1, 0);
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, screen_w, screen_h);
    rect.x = rect.y = 0, rect.w = screen_w, rect.h = screen_h;
#endif
    // decoder -> encoder -> store.
    int64_t start_time = av_gettime();
    printf("%d\n", start_time);
    // step1: video
    int i = 0;
    while (i++ < 2000) {
        // deal with video
        if (encode_video && 
            (!encode_audio || av_compare_ts(vid_next_pts, time_base_q, aud_next_pts, time_base_q) <= 0)) {
            if (av_read_frame(fmt_ctx_v, pkt) < 0) {
                printf("Failed to read frame from Carmer\n");
                return -1;
            }
            if (avcodec_decode_video2(codec_ctx_v, frame, &got_frame, pkt) < 0) {
                printf("Failed to decode the pkt\n");
                return -1;
            }
            if (got_frame) {
                // we must Multiplied by the frame rate, otherwise the duration is chaotic.
                frameYUV->pts = frameYUV->pkt_dts = (++pckCount) * 25;
                frameYUV->width = ocodec_ctx_v->width, frameYUV->height= ocodec_ctx_v->height;
                frameYUV->format = ocodec_ctx_v->pix_fmt;
                // we must make sure the input format is yuv420 for libx264
                sws_scale(pic_convert_ctx, (const uint8_t*const*)frame->data, frame->linesize,
                        0, ocodec_ctx_v->height, frameYUV->data, frameYUV->linesize);
            }
            if (avcodec_encode_video2(ocodec_ctx_v, pktEnc, frameYUV, &got_pkt) < 0) {
                printf("Failed to encode the frame.\n");
                return -1;
            }
            if (got_pkt) {
                framecnt++;
                pktEnc->stream_index = video_st->index;
                //write pts
                AVRational time_base = video_st->time_base;
                AVRational framerate_v = fmt_ctx_v->streams[video_idx]->r_frame_rate;
                // Duration base on AV_TIME_BASE (FFmpeg internal clock)
                int64_t calc_duration = (double)(AV_TIME_BASE) * (1 / av_q2d(framerate_v));
                pktEnc->pts = av_rescale_q(framecnt * calc_duration, time_base_q, time_base);
                pktEnc->dts = pktEnc->pts;
                pktEnc->duration = av_rescale_q(calc_duration, time_base_q, time_base);
                pktEnc->pos = -1;
                printf("%d\n", calc_duration);

                // next pts
                vid_next_pts = framecnt * calc_duration;

                // Delay
                int64_t pts_time = av_rescale_q(pktEnc->pts, time_base, time_base_q);
                int64_t now_time = av_gettime() - start_time;              
                if ((pts_time > now_time) && ((vid_next_pts + pts_time - now_time) < aud_next_pts)) {
                    av_usleep(pts_time - now_time);
                }

                if (av_interleaved_write_frame(ofmt_ctx, pktEnc) < 0) {
                    printf("Failed to store the pkt.\n");
                    return -1;
                }
                av_packet_unref(pkt), av_packet_unref(pktEnc);
            }
        }
        else {
            // deal with audio
            const int output_frame_size = ocodec_ctx_a->frame_size;
            // If there are not enough data for the encoder, get more from the input
            if (av_audio_fifo_size(fifo) < output_frame_size) {
                AVFrame* audio_input_frame = av_frame_alloc();
                if (!audio_input_frame) {
                    printf("Failed to alloc the input audio frame\n");
                    return -1;
                }
                AVPacket audio_input_pkt;
                av_init_packet(&audio_input_pkt);
                audio_input_pkt.data = NULL;
                audio_input_pkt.size = 0;
                // Read one audio frame from the carmer
                if ((ret = av_read_frame(fmt_ctx_a, &audio_input_pkt)) < 0) {
                    if (ret == AVERROR_EOF)
                        encode_audio = 0;
                    else {
                        printf("Failed to read audio frame\n");
                        return ret;
                    }
                }
                // Decode the input audio frame and store into fifo
                if ((ret = avcodec_decode_audio4(codec_ctx_a, audio_input_frame, &got_frame, 
                     &audio_input_pkt)) < 0) {
                    printf("Failed to decode audio frame\n");
                    return ret;
                }
                av_packet_unref(&audio_input_pkt);
                if (got_frame) {
                    // Allocate memory for the samples of all channels in one consecutive
				    // block for convenience.
                    if ((ret = av_samples_alloc(converted_input_samples, NULL, ocodec_ctx_a->channels,
                         audio_input_frame->nb_samples, ocodec_ctx_a->sample_fmt, 0)) < 0) {
                        printf("Failed to allocate converted input samples\n");
                        av_freep(&(*converted_input_samples)[0]);
                        free(*converted_input_samples);
                        return ret;    
                    }
                    // Convert the samples using the resampler
                    if ((ret = swr_convert(aud_convert_ctx, converted_input_samples,
                         audio_input_frame->nb_samples, (const uint8_t**)audio_input_frame->extended_data,
                         audio_input_frame->nb_samples)) < 0) {
                         printf("Failed to convert the input audio samples\n");
                         return ret;
                    }
                    if ((ret = av_audio_fifo_realloc(fifo, av_audio_fifo_size(fifo) +
                         audio_input_frame->nb_samples)) < 0) {
                         printf("Failed to reallocate fifo.\n");
                         return ret;
                    }
                    // Store th new samples in fifo buffer
                    if (av_audio_fifo_write(fifo, (void**)converted_input_samples,
                        audio_input_frame->nb_samples) < audio_input_frame->nb_samples) {
                        printf("Failed to write audio data to fifo.\n");
                        return -1;
                    }
                }
            }
            // If we have enough audio data for the encoder.
            if (av_audio_fifo_size(fifo) >= output_frame_size) {
                AVFrame* audio_output_frame = av_frame_alloc();
                if (!audio_output_frame) {
                    printf("Failed to alloctate the audio output frame.\n");
                    return -1;
                } 
                const int frame_size = FFMIN(av_audio_fifo_size(fifo), output_frame_size);
                // Set the frame params
                audio_output_frame->nb_samples = frame_size;
                audio_output_frame->channel_layout = ocodec_ctx_a->channel_layout;
                audio_output_frame->format = ocodec_ctx_a->sample_fmt;
                audio_output_frame->sample_rate = ocodec_ctx_a->sample_rate;
                if ((ret = av_frame_get_buffer(audio_output_frame, 0)) < 0) {
                    printf("Failed to allocate audio output buffer.\n");
                    av_frame_free(&audio_output_frame);
                    return -1;
                }
                // Read data from fifo to fill the output frame
                if (av_audio_fifo_read(fifo, (void **)audio_output_frame->data, frame_size) < frame_size) {
                    printf("Failed to read audio data from fifo to fill output frame\n");
                    return -1;
                }
                // 
                AVPacket audio_output_pkt;
                av_init_packet(&audio_output_pkt);
                audio_output_pkt.data = NULL, audio_output_pkt.size = 0;
                if (audio_output_frame) {
                    nb_samples += audio_output_frame->nb_samples;
                }
                if ((ret = avcodec_encode_audio2(ocodec_ctx_a, &audio_output_pkt,
                     audio_output_frame, &got_pkt)) < 0) {
                     printf("Failed to encode the audio frame.\n");
                     return -1;
                }
                if (got_pkt) {
                    audio_output_pkt.stream_index = 1;
                    AVRational time_base = audio_st->time_base;
                    AVRational r_framerate1 = {fmt_ctx_a->streams[audio_idx]->codecpar->sample_rate, 1};
                    int64_t calac_duration = (double) (AV_TIME_BASE) * (1 / av_q2d(r_framerate1));
                    printf("%d\n", calac_duration);

                    audio_output_pkt.pts = av_rescale_q(nb_samples * calac_duration, time_base_q, time_base);
                    audio_output_pkt.dts = audio_output_pkt.pts;
                    //audio_output_pkt.duration = audio_output_frame->nb_samples;
                    audio_output_pkt.duration = av_rescale_q(calac_duration, time_base_q, time_base);

                    aud_next_pts = nb_samples * calac_duration;
                    int64_t pts_time = av_rescale_q(audio_output_pkt.pts, time_base, time_base_q);
                    int64_t now_time = av_gettime() - start_time;
                    if ((pts_time > now_time) && ((aud_next_pts + pts_time - now_time)) < vid_next_pts) {
                        av_usleep(pts_time - now_time);
                    }
                    if ((ret = av_interleaved_write_frame(ofmt_ctx, &audio_output_pkt)) < 0) {
                        printf("Failed to write audio pkt.\n");
                        return -1;
                    }
                }
                av_packet_unref(&audio_output_pkt);
            }
        }
    }
    av_packet_free(&pktEnc);
    av_packet_free(&pkt);
    av_frame_free(&frame);
    av_frame_free(&frameYUV);
    av_write_trailer(ofmt_ctx);
    avformat_close_input(&fmt_ctx_v);
    avformat_close_input(&ofmt_ctx);
    avio_close(ofmt_ctx->pb);
    av_free(buffer);
    return 0;
}