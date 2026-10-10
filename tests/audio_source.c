/* A delayed producer must not let the VP read unpublished ring frames. */
#include "../src/audio_service_trace.c"
#include <assert.h>
MCPXAPUState *g_state;
uintptr_t g_xbox_mem_offset;
BOOL QueryPerformanceFrequency(LARGE_INTEGER *x){x->QuadPart=1000000;return TRUE;}
BOOL QueryPerformanceCounter(LARGE_INTEGER *x){x->QuadPart=0;return TRUE;}
static void put(uint32_t a,uint32_t v){memcpy((void *)(g_xbox_mem_offset+a),&v,4);}
static void cbo(unsigned n){memcpy(g_state->ram_ptr+0x1000+66*128+0x58,&n,4);}
int main(void)
{
    assert(ssx_audio_stream_lead(960)==2880);
    assert(ssx_audio_stream_ring(2400)==4800);
    assert(ssx_audio_stream_lead(441)==441);
    assert(ssx_audio_stream_ring(2205)==2205);
    unsigned char *ram=calloc(1,0x210000);assert(ram);g_xbox_mem_offset=(uintptr_t)ram;
    g_state=calloc(1,sizeof *g_state);assert(g_state);g_state->ram_ptr=ram;
    g_state->regs[NV_PAPU_VPVADDR]=0x1000;
    put(0x205bb0,0x201c);put(0x2020,0x3000);put(0x3010,66);put(0x205be0,2400);
    uint32_t base=0x1000+66*128;
    put(base+4,0x52000000);put(base+0x54,NV_PAVS_VOICE_PAR_STATE_ACTIVE_VOICE);put(base+0x5c,2399);
    assert(ssx_audio_source_ready()); /* Before the software producer exists. */
    ssx_audio_service_publish(992);
    for(unsigned n=0;n<=736;n+=32){cbo(n);assert(ssx_audio_source_ready());}
    cbo(768);assert(!ssx_audio_source_ready()); /* 224 ready, need256. */
    ssx_audio_service_publish(1760);assert(ssx_audio_source_ready());
    cbo(1728);assert(!ssx_audio_source_ready());
    ssx_audio_service_publish(320);assert(ssx_audio_source_ready()); /* Wrapped publication. */
    cbo(224);assert(!ssx_audio_source_ready());
    put(base+0x54,0);assert(ssx_audio_source_ready()); /* Stopped voice cannot deadlock. */
    put(base+0x54,NV_PAVS_VOICE_PAR_STATE_ACTIVE_VOICE);
    put(0x205bb0,0x401c);assert(ssx_audio_source_ready()); /* Changed device. */
    put(0x205bb0,0x201c);put(base+4,0x5a000000);assert(ssx_audio_source_ready()); /* Stereo is outside this guard. */
    put(base+4,0x52000000);put(base+0x5c,4799);put(0x205be0,4800);
    cbo(4608);ssx_audio_service_publish(2688);assert(ssx_audio_source_ready());
    cbo(2464);assert(!ssx_audio_source_ready());
    ssx_audio_service_publish(512);assert(ssx_audio_source_ready());
    free(g_state);g_state=NULL;free(ram);puts("Delayed, wrapped, stopped and changed software source publication passed");
    return 0;
}
