#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
extern uintptr_t g_xbox_mem_offset;
/* Read-only, bounded service timing for the supported retail XBE. */
#include "apu_state.h"
void ssx_audio_service_trace(unsigned event, unsigned cursor)
{
    static int initialized;static FILE *output;static unsigned count;
    if(!initialized) {
        initialized=1;const char *path=getenv("RECOMP_AUDIO_SERVICE_TRACE");
        if(path && *path)output=fopen(path,"w");
        if(output)fprintf(output,"us,event,cursor,previous,lead,ring,cbo64,cbo66,frame\n");
    }
    if(!output || count>=20000)return;
    uint32_t previous,lead,ring,cbo64=0,cbo66=0;
    memcpy(&previous,(void *)(g_xbox_mem_offset+0x205be8),4);
    memcpy(&lead,(void *)(g_xbox_mem_offset+0x205be4),4);
    memcpy(&ring,(void *)(g_xbox_mem_offset+0x205be0),4);
    if(g_state) {
        uintptr_t base=(uintptr_t)g_state->ram_ptr+g_state->regs[NV_PAPU_VPVADDR];
        memcpy(&cbo64,(void *)(base+64*128+0x58),4);
        memcpy(&cbo66,(void *)(base+66*128+0x58),4);
    }
    fprintf(output,"%lld,%u,%u,%u,%u,%u,%u,%u,%u\n",
        (long long)qemu_clock_get_us(QEMU_CLOCK_REALTIME),event,cursor,previous,
        lead,ring,cbo64&0xffffff,cbo66&0xffffff,g_state?g_state->ep_frame_div:0);
    if(++count%100==0)fflush(output);
    if(count==20000){fclose(output);output=NULL;}
}

#include <stdatomic.h>
/* Publication metadata lives in host RAM. The guest's buffers stay untouched.
 * One service thread publishes all six buses after their PCM stores finish. */
static _Atomic uint32_t source_epoch,source_buffer,source_voice,source_table;
static _Atomic uint32_t source_end,source_ring;
static int ram_u32(uint32_t address,uint32_t *value)
{
    if(address>0x3fffffcu && (address<0x80000000u || address>0x83fffffcu))return 0;
    memcpy(value,(void *)(g_xbox_mem_offset+(uintptr_t)address),4);return 1;
}
void ssx_audio_service_publish(unsigned end)
{
    uint32_t buffer,object,voice,ring;
    if(!g_state || !ram_u32(0x205bb0,&buffer) || buffer<0x1c ||
       !ram_u32(buffer-0x1c+0x20,&object) || !ram_u32(object+0x10,&voice) ||
       voice>=256 || !ram_u32(0x205be0,&ring) || !ring || end>=ring)return;
    atomic_fetch_add_explicit(&source_epoch,1,memory_order_acq_rel);
    atomic_store_explicit(&source_buffer,buffer,memory_order_relaxed);
    atomic_store_explicit(&source_voice,voice,memory_order_relaxed);
    atomic_store_explicit(&source_table,g_state->regs[NV_PAPU_VPVADDR],memory_order_relaxed);
    atomic_store_explicit(&source_ring,ring,memory_order_relaxed);
    atomic_store_explicit(&source_end,end,memory_order_relaxed);
    atomic_fetch_add_explicit(&source_epoch,1,memory_order_release);
}
int ssx_audio_source_ready(void)
{
    if(!g_state)return 1;
    unsigned epoch=atomic_load_explicit(&source_epoch,memory_order_acquire);
    if(!epoch)return 1;
    if(epoch&1)return 0;
    uint32_t buffer=atomic_load_explicit(&source_buffer,memory_order_relaxed);
    uint32_t voice=atomic_load_explicit(&source_voice,memory_order_relaxed);
    uint32_t table=atomic_load_explicit(&source_table,memory_order_relaxed);
    uint32_t ring=atomic_load_explicit(&source_ring,memory_order_relaxed);
    uint32_t end=atomic_load_explicit(&source_end,memory_order_relaxed);
    if(epoch!=atomic_load_explicit(&source_epoch,memory_order_acquire))return 0;
    uint32_t current;
    if(!ram_u32(0x205bb0,&current) || current!=buffer ||
       table!=g_state->regs[NV_PAPU_VPVADDR] || voice>=256 || ring<256 || ring>16384)return 1;
    uintptr_t base=(uintptr_t)g_state->ram_ptr+table+voice*128;
    uint32_t cfg,status,cbo,ebo,lbo,pitch;
    memcpy(&cfg,(void *)(base+4),4);memcpy(&status,(void *)(base+0x54),4);
    memcpy(&cbo,(void *)(base+0x58),4);memcpy(&ebo,(void *)(base+0x5c),4);
    memcpy(&lbo,(void *)(base+0x24),4);memcpy(&pitch,(void *)(base+0x7c),4);
    cbo&=0xffffff;ebo&=0xffffff;lbo&=0xffffff;
    /* Guard only the game's live mono S16/48-kHz circular software buses.
     * Other voices, stopped playback and changed devices retain normal APU behavior. */
    if(!(status&NV_PAVS_VOICE_PAR_STATE_ACTIVE_VOICE) ||
       status&NV_PAVS_VOICE_PAR_STATE_PAUSED || lbo || ebo+1!=ring || cbo>=ring ||
       (pitch>>16) || !(cfg&NV_PAVS_VOICE_CFG_FMT_LOOP) ||
       (cfg&(NV_PAVS_VOICE_CFG_FMT_STEREO|NV_PAVS_VOICE_CFG_FMT_DATA_TYPE|
             NV_PAVS_VOICE_CFG_FMT_SAMPLES_PER_BLOCK)) ||
       GET_MASK(cfg,NV_PAVS_VOICE_CFG_FMT_SAMPLE_SIZE)!=NV_PAVS_VOICE_CFG_FMT_SAMPLE_SIZE_S16 ||
       GET_MASK(cfg,NV_PAVS_VOICE_CFG_FMT_CONTAINER_SIZE)!=1)return 1;
    return (end+ring-cbo)%ring>=256;
}

/* Native Linux scheduling can delay the game's service by a render frame.
 * Configure its ordinary six streaming buffers with 60 ms prepared audio
 * inside a 100 ms ring instead of the retail 20/50 ms. These helpers run at
 * buffer creation, before allocation/playback; they never patch a live ring.
 * Preserve unknown formats and the unvalidated Windows configuration. */
unsigned ssx_audio_stream_lead(unsigned original)
{
#ifndef _WIN32
    if(original==960)return 2880;
#endif
    return original;
}
unsigned ssx_audio_stream_ring(unsigned original)
{
#ifndef _WIN32
    if(original==2400)return 4800;
#endif
    return original;
}
