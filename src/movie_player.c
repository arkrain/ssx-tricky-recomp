/* Decode the user's original EA movie container through FFmpeg. */
#include "movie_player.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef SSX_HAVE_FFMPEG
#include <SDL.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/imgutils.h>
#include <libavutil/opt.h>

typedef struct {
    const SsxMovieOutput *output;
    AVFormatContext *format;
    AVCodecContext *video, *audio;
    struct SwsContext *scale;
    SwrContext *resample;
    SDL_AudioDeviceID device;
    uint32_t *pixels;
    uint64_t started;
    double first_pts, frame_seconds;
    unsigned frames;
    int video_stream, audio_stream, skipped, audio_initialized;
    FILE *ea_audio;
    uint64_t audio_samples;
    int ea_finished;
} Movie;

static int fill_ea_audio(Movie *m,double seconds);
static int movie_skip(Movie *m) {
    if(m->output->skip && m->output->skip(m->output->opaque))m->skipped=1;
    return m->skipped;
}
static int wait_until(Movie *m,double seconds) {
    if(!m->output->realtime)return !movie_skip(m);
    while((SDL_GetPerformanceCounter()-m->started)/(double)SDL_GetPerformanceFrequency()<seconds) {
        if(movie_skip(m))return 0;
        SDL_Delay(2);
    }
    return !movie_skip(m);
}
static int video_frame(Movie *m,AVFrame *frame) {
    double pts=frame->best_effort_timestamp==AV_NOPTS_VALUE?m->frames*m->frame_seconds:
        frame->best_effort_timestamp*av_q2d(m->format->streams[m->video_stream]->time_base);
    if(!m->frames)m->first_pts=pts;
    if(m->ea_audio && !fill_ea_audio(m,pts-m->first_pts+.25))return 0;
    if(!wait_until(m,pts-m->first_pts))return 0;
    AVRational sar=av_guess_sample_aspect_ratio(m->format,m->format->streams[m->video_stream],frame);
    double aspect=(double)frame->width/frame->height*(sar.num>0?av_q2d(sar):1.0);
    int width=640,height=(int)(640/aspect+.5);
    if(height>480){height=480;width=(int)(480*aspect+.5);}
    if(width<1 || height<1 || width>640 || height>480)return 0;
    m->scale=sws_getCachedContext(m->scale,frame->width,frame->height,frame->format,
        width,height,AV_PIX_FMT_BGRA,SWS_BILINEAR,NULL,NULL,NULL);
    if(!m->scale)return 0;
    memset(m->pixels,0,640*480*4);
    uint8_t *dst[4]={(uint8_t *)(m->pixels+((480-height)/2)*640+(640-width)/2)};
    int strides[4]={640*4};
    if(sws_scale(m->scale,(const uint8_t *const *)frame->data,frame->linesize,0,frame->height,dst,strides)!=height)return 0;
    if(m->output->present)m->output->present(m->pixels,m->output->opaque);
    m->frames++;
    return 1;
}
static int audio_frame(Movie *m,AVFrame *frame) {
    m->audio_samples+=frame->nb_samples;
    if(!m->device && !m->output->audio_pcm)return 1;
    if(!m->resample) {
        AVChannelLayout stereo=AV_CHANNEL_LAYOUT_STEREO;
        if(swr_alloc_set_opts2(&m->resample,&stereo,AV_SAMPLE_FMT_S16,48000,
            &frame->ch_layout,frame->format,frame->sample_rate,0,NULL)<0 || swr_init(m->resample)<0)return 0;
    }
    int capacity=(int)av_rescale_rnd(swr_get_delay(m->resample,frame->sample_rate)+frame->nb_samples,
                                   48000,frame->sample_rate,AV_ROUND_UP);
    uint8_t *buffer=NULL;
    if(av_samples_alloc(&buffer,NULL,2,capacity,AV_SAMPLE_FMT_S16,0)<0)return 0;
    const uint8_t *source[8]={0};
    if(m->ea_audio && frame->ch_layout.nb_channels==6) {
        /* EA R1 planes are FL,FC,FR,BL,BR,LFE; the decoder preserves them.
         * swresample expects the declared 5.1 order FL,FR,FC,LFE,BL,BR. */
        static const unsigned order[6]={0,2,1,5,3,4};
        for(unsigned i=0;i<6;i++)source[i]=frame->extended_data[order[i]];
    } else for(int i=0;i<frame->ch_layout.nb_channels && i<8;i++)source[i]=frame->extended_data[i];
    int count=swr_convert(m->resample,&buffer,capacity,source,frame->nb_samples);
    int ok=count>=0;
    if(ok && m->output->audio_pcm)m->output->audio_pcm((const int16_t *)buffer,(unsigned)count,m->output->opaque);
    if(ok && m->device)ok=SDL_QueueAudio(m->device,buffer,(unsigned)count*4)==0;
    av_freep(&buffer);
    return ok;
}
static AVCodecContext *open_decoder(AVFormatContext *format,int index) {
    if(index<0)return NULL;
    AVCodecParameters *parameters=format->streams[index]->codecpar;
    const AVCodec *codec=avcodec_find_decoder(parameters->codec_id);
    if(!codec)return NULL;
    AVCodecContext *context=avcodec_alloc_context3(codec);
    if(!context)return NULL;
    if(avcodec_parameters_to_context(context,parameters)<0 || avcodec_open2(context,codec,NULL)<0)
        avcodec_free_context(&context);
    return context;
}
static int decode(Movie *m,AVCodecContext *codec,AVPacket *packet,AVFrame *frame,int video) {
    if(avcodec_send_packet(codec,packet)<0)return 0;
    int status;
    while((status=avcodec_receive_frame(codec,frame))>=0) {
        int ok=video?video_frame(m,frame):audio_frame(m,frame);
        av_frame_unref(frame);
        if(!ok)return 0;
    }
    return status==AVERROR(EAGAIN) || status==AVERROR_EOF;
}
static uint32_t little32(const uint8_t *p) {
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
/* libavformat's EA demuxer rejects >2 channels, although its R1 decoder
 * supports six. Read only this title's verified PT07/R1 surround chunks in
 * a separate bounded stream; video still uses the normal demuxer/parser. */
static int open_ea_surround(Movie *m,const char *path) {
    FILE *file=fopen(path,"rb");if(!file)return -1;
    int found=0;
    for(unsigned n=0;n<16;n++) {
        uint8_t header[8],payload[4096];
        if(fread(header,1,8,file)!=8)break;
        uint32_t size=little32(header+4);
        if(size<8 || size>64*1024*1024)break;
        if(memcmp(header,"SCHl",4)) { if(fseek(file,size-8,SEEK_CUR))break;continue; }
        if(size-8>sizeof payload || fread(payload,1,size-8,file)!=size-8)break;
        found=1;
        if(size<12 || memcmp(payload,"PT\x07\0",4))break;
        unsigned channels=0,rate=0,revision2=0;
        for(unsigned pos=4;pos<size-8;) {
            unsigned tag=payload[pos++];if(tag==0xff)break;if(tag==0xfd)continue;
            if(pos>=size-8)break;
            unsigned bytes=payload[pos++];if(bytes>size-8-pos)break;
            uint32_t value=0;
            for(unsigned i=0;i<bytes;i++)value=(value<<8)|payload[pos+i];
            pos+=bytes;
            if(tag==0x82)channels=value;if(tag==0x84)rate=value;if(tag==0xa0)revision2=value;
        }
        if(channels!=6 || rate!=48000 || revision2!=10)break;
        const AVCodec *codec=avcodec_find_decoder(AV_CODEC_ID_ADPCM_EA_R1);
        if(!codec)break;
        m->audio=avcodec_alloc_context3(codec);if(!m->audio)break;
        m->audio->sample_rate=rate;
        AVChannelLayout layout=AV_CHANNEL_LAYOUT_5POINT1_BACK;
        av_channel_layout_copy(&m->audio->ch_layout,&layout);
        m->audio->bits_per_coded_sample=16;m->audio->block_align=channels*16;
        if(avcodec_open2(m->audio,codec,NULL)<0){avcodec_free_context(&m->audio);break;}
        m->ea_audio=file;
        fprintf(stderr,"[MOVIE] EA R1 surround audio: 6 channels at 48000 Hz\n");
        return 1;
    }
    fclose(file);return found?-1:0;
}
static int fill_ea_audio(Movie *m,double seconds) {
    AVPacket *packet=av_packet_alloc();AVFrame *frame=av_frame_alloc();int ok=0;
    if(!packet || !frame)goto done;
    while(!m->ea_finished && m->audio_samples/48000.0<seconds) {
        uint8_t header[8];size_t got=fread(header,1,8,m->ea_audio);
        if(!got && feof(m->ea_audio)){m->ea_finished=1;break;}
        if(got!=8)goto done;
        uint32_t size=little32(header+4);
        if(size<8 || size>64*1024*1024)goto done;
        if(!memcmp(header,"SCDl",4)) {
            if(size-8>16*1024*1024 || av_new_packet(packet,size-8)<0)goto done;
            if(fread(packet->data,1,size-8,m->ea_audio)!=size-8 || !decode(m,m->audio,packet,frame,0))goto done;
            av_packet_unref(packet);
        } else {
            if(!memcmp(header,"SCEl",4))m->ea_finished=1;
            if(fseek(m->ea_audio,size-8,SEEK_CUR))goto done;
        }
        if(movie_skip(m))goto done;
    }
    ok=1;
done:
    av_packet_free(&packet);av_frame_free(&frame);return ok;
}
int ssx_movie_play(const char *path,const SsxMovieOutput *output) {
    Movie m={0};m.output=output;m.video_stream=m.audio_stream=-1;
    AVPacket *packet=av_packet_alloc();AVFrame *frame=av_frame_alloc();int ok=0;
    if(!packet || !frame || avformat_open_input(&m.format,path,NULL,NULL)<0 || avformat_find_stream_info(m.format,NULL)<0)goto done;
    m.video_stream=av_find_best_stream(m.format,AVMEDIA_TYPE_VIDEO,-1,-1,NULL,0);
    m.audio_stream=av_find_best_stream(m.format,AVMEDIA_TYPE_AUDIO,-1,-1,NULL,0);
    m.video=open_decoder(m.format,m.video_stream);m.audio=open_decoder(m.format,m.audio_stream);
    if(!m.video || (m.audio_stream>=0 && !m.audio))goto done;
    if(m.audio_stream<0 && open_ea_surround(&m,path)<0)goto done;
    AVRational fps=av_guess_frame_rate(m.format,m.format->streams[m.video_stream],NULL);
    m.frame_seconds=fps.num>0?av_q2d(av_inv_q(fps)):1.0/30;
    m.pixels=malloc(640*480*4);if(!m.pixels)goto done;
    if(output->audio && m.audio) {
#ifdef _WIN32
        SDL_SetMainReady();
#endif
        SDL_AudioSpec spec={0};spec.freq=48000;spec.format=AUDIO_S16SYS;spec.channels=2;spec.samples=1024;
        if(!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
            m.audio_initialized=1;
            m.device=SDL_OpenAudioDevice(NULL,0,&spec,NULL,0);
        }
        if(!m.device){fprintf(stderr,"[MOVIE] audio unavailable: %s\n",SDL_GetError());goto done;}
        SDL_PauseAudioDevice(m.device,0);
    }
    m.started=SDL_GetPerformanceCounter();
    fprintf(stderr,"[MOVIE] Playing %s\n",path);
    int status;
    while((status=av_read_frame(m.format,packet))>=0) {
        if(movie_skip(&m)){av_packet_unref(packet);goto done;}
        int decoded=1;
        if(packet->stream_index==m.video_stream)decoded=decode(&m,m.video,packet,frame,1);
        else if(packet->stream_index==m.audio_stream)decoded=decode(&m,m.audio,packet,frame,0);
        av_packet_unref(packet);
        if(!decoded)goto done;
    }
    if(m.ea_audio && !fill_ea_audio(&m,1e9))goto done;
    if(status!=AVERROR_EOF || !decode(&m,m.video,NULL,frame,1) ||
       (m.audio && !decode(&m,m.audio,NULL,frame,0)))goto done;
    if(m.resample) {
        uint8_t tail[4096],*ptr=tail;int count;
        while((count=swr_convert(m.resample,&ptr,sizeof(tail)/4,NULL,0))>0) {
            if(output->audio_pcm)output->audio_pcm((const int16_t *)tail,(unsigned)count,output->opaque);
            if(m.device && SDL_QueueAudio(m.device,tail,(unsigned)count*4))goto done;
        }
    }
    if(!wait_until(&m,m.frames*m.frame_seconds))goto done;
    while(m.device && output->realtime && SDL_GetQueuedAudioSize(m.device)) {
        if(movie_skip(&m))goto done;
        SDL_Delay(2);
    }
    ok=m.frames>0 && (!m.audio || m.audio_samples>0);
done:
    if(m.device){SDL_ClearQueuedAudio(m.device);SDL_CloseAudioDevice(m.device);}
    if(m.audio_initialized)SDL_QuitSubSystem(SDL_INIT_AUDIO);
    if(m.ea_audio)fclose(m.ea_audio);
    av_frame_free(&frame);av_packet_free(&packet);sws_freeContext(m.scale);swr_free(&m.resample);
    avcodec_free_context(&m.video);avcodec_free_context(&m.audio);avformat_close_input(&m.format);free(m.pixels);
    fprintf(stderr,"[MOVIE] %s: %u video frames, %llu audio samples (%s)\n",path,m.frames,(unsigned long long)m.audio_samples,m.skipped?"skipped":ok?"complete":"failed");
    return m.skipped?2:ok;
}
#else
int ssx_movie_play(const char *path,const SsxMovieOutput *output){(void)path;(void)output;return 0;}
#endif
