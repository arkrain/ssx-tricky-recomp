/* Exercise the real runtime callbacks without opening an audio device. */
#include "../src/apu_dsp_runtime.c"
#include <assert.h>
int main(void) {
    MCPXAPUState *d=calloc(1,sizeof *d);assert(d);
    d->ram_ptr=calloc(1,0x4000000);assert(d->ram_ptr);
    g_apu_low_ram_ptr=calloc(1,4096);assert(g_apu_low_ram_ptr);g_apu_low_ram_size=4096;
    d->monitor.point=MCPX_APU_DEBUG_MON_GP_OR_EP;
    assert(initialize(d));
    float bins[32][32]={{0}};
    memset(d->monitor.frame_buf,0x55,sizeof d->monitor.frame_buf);d->ep_frame_div=3;
    frame(d,bins);
    for(unsigned i=0;i<256;i++)for(unsigned c=0;c<2;c++)
        assert(d->monitor.frame_buf[i][c]==(i>=96 && i<128?0:0x5555));
    /* Bootstrap through the ordinary-RAM descriptor window into physical RAM. */
    g_apu_low_ram_ptr[1]=0x20;g_apu_low_ram_ptr[9]=0x30;
    uint32_t program[]={0x08f484,1,0x0c0000}; /* halt, then jump back next frame */
    memcpy(d->ram_ptr+0x2000,program,sizeof program);
    d->regs[NV_PAPU_GPSADDR]=0x40000000;d->regs[NV_PAPU_GPSMAXSGE]=1;
    write_register(d,0x3fffc,3,4);assert(!runtime->failed);
    for(unsigned i=0;i<32;i++){bins[0][i]=.25f;bins[1][i]=-.25f;}
    frame(d,bins);assert(!runtime->failed);
    for(unsigned i=96;i<128;i++) {
        assert(d->monitor.frame_buf[i][0]==8192);
        assert(d->monitor.frame_buf[i][1]==-8192);
    }
    frame(d,bins);assert(!runtime->failed);
    /* EP output must reach both the guest FIFO ring and the PCM monitor.
       Start sixteen bytes before ring end to exercise wrapped writeback. */
    uint8_t pcm[1024];for(unsigned i=0;i<sizeof pcm;i++)pcm[i]=(uint8_t)(i*13+7);
    d->regs[NV_PAPU_EPFADDR]=0x40000000;d->regs[NV_PAPU_EPFMAXSGE]=1;
    d->regs[NV_PAPU_EPOFBASE0]=0;d->regs[NV_PAPU_EPOFBASE0+4]=0x2000;
    d->regs[NV_PAPU_EPOFBASE0+8]=0xab001ff0;
    fifo_rw(&runtime->processors[1],pcm,0,sizeof pcm,true);
    assert(!runtime->failed);
    assert(d->regs[NV_PAPU_EPOFBASE0+8]==0xab0003f0);
    assert(!memcmp(d->ram_ptr+0x3ff0,pcm,16));
    assert(!memcmp(d->ram_ptr+0x2000,pcm+16,sizeof pcm-16));
    assert(!memcmp(d->monitor.frame_buf,pcm,sizeof pcm));
    /* Selecting VP monitoring suppresses EP monitor replacement, not DMA. */
    d->monitor.point=MCPX_APU_DEBUG_MON_VP;memset(pcm,0x33,sizeof pcm);
    fifo_rw(&runtime->processors[1],pcm,0,sizeof pcm,true);
    assert(!runtime->failed);
    assert(!memcmp(d->ram_ptr+0x23f0,pcm,sizeof pcm));
    assert(((uint8_t *)d->monitor.frame_buf)[0]==7);
    d->monitor.point=MCPX_APU_DEBUG_MON_GP_OR_EP;
    write_register(d,0x30001,0,4);assert(runtime->failed);
    frame(d,bins);
    for(unsigned i=0;i<256;i++)for(unsigned c=0;c<2;c++)assert(!d->monitor.frame_buf[i][c]);
    reset(d);assert(!runtime->failed && !ssx_dsp_mmio_enabled(&runtime->processors[0].mmio));
    destroy(d);free(d->ram_ptr);free(d);free(g_apu_low_ram_ptr);g_apu_low_ram_ptr=NULL;
    puts("DSP runtime GP output, EP FIFO/monitor, disjoint bootstrap, disabled/fault silence and reset passed");return 0;
}
