/* Exercise backend queue ordering and recovery with a deterministic device.
 * The fake consumer removes real PCM blocks; no sound hardware or sleeps. */
#include <SDL.h>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
#include <string.h>
static int paused,starts,fail_next;
static unsigned used,next_written,next_read;
static uint32_t pcm[4096];
static SDL_AudioDeviceID fixture_open(const char *name,int capture,const SDL_AudioSpec *wanted,SDL_AudioSpec *got,int changes) {
    (void)name;assert(!capture && !changes);
    assert(wanted->freq==48000 && wanted->format==AUDIO_S16SYS && wanted->channels==2 && wanted->samples==512);
    *got=*wanted;paused=1;used=next_written=next_read=0;return 1;
}
static void fixture_close(SDL_AudioDeviceID id){assert(id==1);used=0;}
static void fixture_pause(SDL_AudioDeviceID id,int pause){assert(id==1);paused=pause;if(!pause)starts++;}
static Uint32 fixture_size(SDL_AudioDeviceID id){assert(id==1);return used*4;}
static int fixture_queue(SDL_AudioDeviceID id,const void *samples,Uint32 bytes){
    assert(id==1 && !(bytes%4) && used+bytes/4<=4096);
    if(fail_next){fail_next=0;return -1;}
    memcpy(pcm+used,samples,bytes);used+=bytes/4;return 0;
}
#define SDL_OpenAudioDevice fixture_open
#define SDL_CloseAudioDevice fixture_close
#define SDL_PauseAudioDevice fixture_pause
#define SDL_GetQueuedAudioSize fixture_size
#define SDL_QueueAudio fixture_queue
#include "../third_party/xboxrecomp/src/apu/apu_xaudio2.c"
static void block(void){
    uint32_t data[256];for(unsigned i=0;i<256;i++)data[i]=next_written+i+1;
    assert(xa2_submit_samples((const int16_t*)data,256));next_written+=256;
}
static void consume(unsigned frames){
    assert(frames<=used && !paused);
    for(unsigned i=0;i<frames;i++)assert(pcm[i]==next_read+i+1);
    next_read+=frames;used-=frames;memmove(pcm,pcm+frames,used*4);
}
int main(void){
    SDL_setenv("SDL_AUDIODRIVER","dummy",1);assert(xa2_init());assert(xa2_init());
    assert(paused && xa2_get_target_frames()==2048 && xa2_get_queued_frames()==0);
    for(unsigned i=0;i<7;i++){block();assert(paused);}
    block();assert(!paused && starts==1 && used==2048);
    for(unsigned cycle=0;cycle<100;cycle++){consume(512);block();block();assert(used==2048 && starts==1);}
    /* Scheduling starvation must prebuffer again and retain sample order. */
    consume(2048);block();assert(paused);
    for(unsigned i=0;i<6;i++){block();assert(paused);}
    block();assert(!paused && starts==2 && used==2048 && s_underruns==1);
    /* Queue failure and a bounded overflow must not claim acceptance. */
    uint32_t untouched[256]={0};fail_next=1;assert(!xa2_submit_samples((const int16_t*)untouched,256));assert(used==2048);
    for(unsigned i=0;i<8;i++)block();assert(used==4096);
    assert(!xa2_submit_samples((const int16_t*)untouched,256));assert(used==4096 && s_rejected==2);
    consume(4096);assert(next_read==next_written);
    xa2_shutdown();assert(xa2_get_target_frames()==-1 && xa2_get_queued_frames()==-1);
    assert(xa2_init());assert(paused && s_underruns==0 && s_rejected==0);xa2_shutdown();
    puts("device prebuffer, PCM order, recovery, failures and bounded latency passed");return 0;
}
