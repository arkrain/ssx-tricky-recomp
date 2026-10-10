/* Exercise the real VP reader/resampler against synthetic Xbox PCM buffers. */
#include "../third_party/xboxrecomp/src/apu/apu_vp.c"
#include <assert.h>
static void setup(MCPXAPUState *d, int stereo, int length, int loop)
{
    memset(d,0,sizeof(*d));memset(g_apu_ram_ptr,0,65536);
    d->regs[NV_PAPU_VPVADDR]=0x1000;
    d->regs[NV_PAPU_VPSGEADDR]=0x2000;
    d->regs[NV_PAPU_FENADDR]=0x8000;
    stl_le_phys(address_space_memory,0x2000,0x3000);
    voice_set_mask(d,0,NV_PAVS_VOICE_CFG_FMT,NV_PAVS_VOICE_CFG_FMT_SAMPLE_SIZE,NV_PAVS_VOICE_CFG_FMT_SAMPLE_SIZE_S16);
    voice_set_mask(d,0,NV_PAVS_VOICE_CFG_FMT,NV_PAVS_VOICE_CFG_FMT_CONTAINER_SIZE,1);
    voice_set_mask(d,0,NV_PAVS_VOICE_CFG_FMT,NV_PAVS_VOICE_CFG_FMT_STEREO,stereo);
    voice_set_mask(d,0,NV_PAVS_VOICE_CFG_FMT,NV_PAVS_VOICE_CFG_FMT_SAMPLES_PER_BLOCK,stereo);
    voice_set_mask(d,0,NV_PAVS_VOICE_CFG_FMT,NV_PAVS_VOICE_CFG_FMT_LOOP,loop);
    voice_set_mask(d,0,NV_PAVS_VOICE_PAR_NEXT,NV_PAVS_VOICE_PAR_NEXT_EBO,length-1);
    voice_set_mask(d,0,NV_PAVS_VOICE_PAR_STATE,NV_PAVS_VOICE_PAR_STATE_ACTIVE_VOICE,1);
    for(int i=0;i<length;i++) {
        stw_le_phys(address_space_memory,0x3000+i*2*(1+stereo),i*100);
        if(stereo)stw_le_phys(address_space_memory,0x3000+i*4+2,-i*100);
    }
}
static void near(float value,float expected) { assert(fabsf(value-expected)<0.00001f); }
static HANDLE vp_entered, vp_done;
static DWORD WINAPI locked_frame(void *opaque)
{
    MCPXAPUState *d=opaque;float bins[32][32]={{0}};
    qemu_mutex_lock(&d->lock);SetEvent(vp_entered);
    mcpx_apu_vp_frame(d,bins);
    qemu_mutex_unlock(&d->lock);SetEvent(vp_done);return 0;
}
static void check_voice_lock(MCPXAPUState *d,int pause)
{
    setup(d,0,512,1);qemu_mutex_init(&d->lock);qemu_cond_init(&d->cond);
    d->regs[NV_PAPU_TVL2D]=0;d->regs[NV_PAPU_TVL3D]=d->regs[NV_PAPU_TVLMP]=0xffff;
    voice_set_mask(d,0,NV_PAVS_VOICE_TAR_PITCH_LINK,NV_PAVS_VOICE_TAR_PITCH_LINK_NEXT_VOICE_HANDLE,0xffff);
    voice_lock(d,0,true);
    vp_entered=CreateEventW(NULL,TRUE,FALSE,NULL);vp_done=CreateEventW(NULL,TRUE,FALSE,NULL);
    HANDLE thread=CreateThread(NULL,0,locked_frame,d,0,NULL);assert(thread);
    assert(WaitForSingleObject(vp_entered,1000)==WAIT_OBJECT_0);
    assert(WaitForSingleObject(vp_done,30)==WAIT_TIMEOUT);
    qemu_mutex_lock(&d->lock);
    assert(voice_get_mask(d,0,NV_PAVS_VOICE_PAR_OFFSET,NV_PAVS_VOICE_PAR_OFFSET_CBO)==0);
    if(pause){d->pause_requested=true;qemu_cond_signal(&d->cond);}
    qemu_mutex_unlock(&d->lock);
    if(!pause)voice_lock(d,0,false);
    assert(WaitForSingleObject(vp_done,1000)==WAIT_OBJECT_0);
    assert(WaitForSingleObject(thread,1000)==WAIT_OBJECT_0);
    assert(voice_get_mask(d,0,NV_PAVS_VOICE_PAR_OFFSET,NV_PAVS_VOICE_PAR_OFFSET_CBO)==(pause?0:32));
    CloseHandle(thread);CloseHandle(vp_entered);CloseHandle(vp_done);qemu_mutex_destroy(&d->lock);
}
int main(void)
{
    MCPXAPUState *d=calloc(1,sizeof(*d));assert(d);
    g_apu_ram_ptr=calloc(1,65536);assert(g_apu_ram_ptr);
    float out[32][2];
    /* Inclusive EBO: a three-sample loop must retain its final sample. */
    setup(d,0,3,1);
    for(int i=0;i<12;i++) {
        assert(voice_get_samples(d,0,out,1)==1);
        near(out[0][0],(i%3)*100/32768.0f);
    }
    const float rates[]={1.0f,2.0f,0.5f,1.5f};
    for(int stereo=0;stereo<2;stereo++)for(int r=0;r<4;r++) {
        setup(d,stereo,512,1);
        /* Separate calls ensure fractional phase survives frame boundaries. */
        for(int block=0;block<4;block++) {
            assert(voice_resample(d,0,out,17,rates[r])==17);
            for(int i=0;i<17;i++) {
                float expected=(block*17+i)/rates[r]*100/32768.0f;
                near(out[i][0],expected);near(out[i][1],stereo?-expected:expected);
            }
        }
    }
    /* Temporary starvation must not lose the pending source position. */
    setup(d,0,512,1);
    assert(voice_resample(d,0,out,3,1.0f)==3);
    voice_set_mask(d,0,NV_PAVS_VOICE_PAR_STATE,NV_PAVS_VOICE_PAR_STATE_PAUSED,1);
    assert(voice_resample(d,0,out,3,1.0f)==0);
    voice_set_mask(d,0,NV_PAVS_VOICE_PAR_STATE,NV_PAVS_VOICE_PAR_STATE_PAUSED,0);
    assert(voice_resample(d,0,out,3,1.0f)==3);
    near(out[0][0],300/32768.0f);
    /* A finite source keeps its final sample and terminates without spinning. */
    setup(d,0,5,0);
    assert(voice_resample(d,0,out,32,2.0f)==10);
    for(int i=0;i<10;i++)near(out[i][0],fminf(i/2.0f,4)*100/32768.0f);
    assert(voice_resample(d,0,out,32,2.0f)==0);
    setup(d,0,512,1);
    assert(voice_resample(d,0,out,3,2.0f)==3);
    voice_set_mask(d,0,NV_PAVS_VOICE_PAR_OFFSET,NV_PAVS_VOICE_PAR_OFFSET_CBO,0);
    voice_reset_filters(d,0);
    assert(voice_resample(d,0,out,2,2.0f)==2);
    near(out[0][0],0);near(out[1][0],50/32768.0f);
    check_voice_lock(d,0);check_voice_lock(d,1);
    free(g_apu_ram_ptr);g_apu_ram_ptr=NULL;free(d);return 0;
}
