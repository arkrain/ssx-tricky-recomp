/* Experimental connection between the standalone APU and DSP interpreter. */
#include "apu.h"
#include "apu_state.h"
#include "fpconv.h"
#include "apu_dsp_backend.h"
#include "apu_dsp_dma.h"
#include "apu_dsp_mmio.h"
typedef struct {SsxDspMmio mmio;unsigned ep;} Processor;
static struct {MCPXAPUState *apu;Processor processors[2];bool failed;unsigned shown[2];} *runtime;
static uint8_t *resolve_bus(void *opaque,uint64_t address,size_t length) {
    MCPXAPUState *d=opaque;
    if(address>=0x40000000 && address<0x80000000) {
        uint64_t offset=address-0x40000000;
        if(!g_apu_low_ram_ptr || offset>g_apu_low_ram_size || length>g_apu_low_ram_size-offset)return NULL;
        return g_apu_low_ram_ptr+(size_t)offset;
    }
    if(address>0x4000000 || length>0x4000000-address)return NULL;
    return d->ram_ptr+(size_t)address;
}
static void fault(const char *message) {
    if(!runtime->failed)fprintf(stderr,"[DSP] execution disabled: %s\n",message);
    runtime->failed=true;
}
static void scratch_rw(void *opaque,uint8_t *data,uint32_t address,size_t length,bool write) {
    Processor *p=opaque;MCPXAPUState *d=runtime->apu;
    unsigned base=p->ep?NV_PAPU_EPSADDR:NV_PAPU_GPSADDR;
    unsigned max=p->ep?NV_PAPU_EPSMAXSGE:NV_PAPU_GPSMAXSGE;
    if(!ssx_dsp_sg_bus(resolve_bus,d,d->regs[base],d->regs[max],
                           data,address,length,write)) {
        if(!write)memset(data,0,length);
        fault("invalid scratch DMA mapping");
    }
}
static void fifo_rw(void *opaque,uint8_t *data,unsigned index,size_t length,bool write) {
    Processor *p=opaque;MCPXAPUState *d=runtime->apu;
    if(index>=(write?4u:2u)) {fault("invalid FIFO index");return;}
    unsigned first=p->ep?(write?NV_PAPU_EPOFBASE0:NV_PAPU_EPIFBASE0):
                         (write?NV_PAPU_GPOFBASE0:NV_PAPU_GPIFBASE0);
    unsigned reg=first+index*16;
    uint32_t base=d->regs[reg]&0xffffff,end=d->regs[reg+4]&0xffffff;
    uint32_t current=d->regs[reg+8]&0xffffff;
    if(p->ep && end>base && current>=end)current=base+(current-base)%(end-base);
    unsigned table=p->ep?NV_PAPU_EPFADDR:NV_PAPU_GPFADDR;
    unsigned max=p->ep?NV_PAPU_EPFMAXSGE:NV_PAPU_GPFMAXSGE;
    if(!ssx_dsp_ring_bus(resolve_bus,d,d->regs[table],d->regs[max],
                             data,base,end,&current,length,write)) {
        if(!write)memset(data,0,length);
        fault("invalid FIFO DMA mapping");return;
    }
    d->regs[reg+8]=(d->regs[reg+8]&0xff000000)|current;
    if(p->ep && write && !index && d->monitor.point!=MCPX_APU_DEBUG_MON_VP) {
        if(length!=sizeof d->monitor.frame_buf) {fault("unexpected EP PCM block size");return;}
        memcpy(d->monitor.frame_buf,data,length);
    }
}
static int initialize(MCPXAPUState *d) {
    runtime=calloc(1,sizeof *runtime);if(!runtime)return 0;
    runtime->apu=d;
    for(unsigned i=0;i<2;i++) {
        Processor *p=&runtime->processors[i];p->ep=i;
        SsxDsp *backend=ssx_dsp_create(p,scratch_rw,fifo_rw,!i);
        ssx_dsp_mmio_init(&p->mmio,backend,!i);
    }
    d->gp.realtime=d->ep.realtime=true;
    fprintf(stderr,"[DSP] real GP/EP interpreter enabled (experimental)\n");return 1;
}
static void destroy(MCPXAPUState *d) {
    (void)d;for(unsigned i=0;i<2;i++)ssx_dsp_destroy(runtime->processors[i].mmio.backend);
    free(runtime);runtime=NULL;
}
static void reset(MCPXAPUState *d) {
    for(unsigned i=0;i<2;i++) {
        SsxDspMmio *m=&runtime->processors[i].mmio;SsxDsp *backend=m->backend;
        ssx_dsp_reset(backend);ssx_dsp_mmio_init(m,backend,!i);
    }
    runtime->failed=false;d->ep_frame_div=0;
}
static uint32_t read_register(MCPXAPUState *d,uint32_t address,unsigned size) {
    (void)d;uint32_t value=0;unsigned ep=address>=0x50000;
    if(!ssx_dsp_mmio_read(&runtime->processors[ep].mmio,address&0xffff,size,&value))
        fault("invalid DSP register read");
    return value;
}
static void write_register(MCPXAPUState *d,uint32_t address,uint32_t value,unsigned size) {
    unsigned ep=address>=0x50000;
    if(!ssx_dsp_mmio_write(&runtime->processors[ep].mmio,address&0xffff,size,value))
        fault("invalid DSP register write");
    if(ep && (address&0xffff)==0xfffc)d->ep_frame_div=0;
}
static bool run(Processor *p) {
    unsigned cycles=0;
    if(!ssx_dsp_frame(p->mmio.backend,1000000,&cycles)) {
        fault(p->ep?"EP frame exceeded cycle budget":"GP frame exceeded cycle budget");return false;
    }
    if(!runtime->shown[p->ep]++)fprintf(stderr,"[DSP] %s frame halted after %u cycles\n",p->ep?"EP":"GP",cycles);
    if(p->ep)g_dbg.ep.cycles=cycles;else g_dbg.gp.cycles=cycles;
    return !runtime->failed;
}
static void frame(MCPXAPUState *d,float bins[32][32]) {
    Processor *gp=&runtime->processors[0],*ep=&runtime->processors[1];
    if(runtime->failed) {memset(d->monitor.frame_buf,0,sizeof d->monitor.frame_buf);return;}
    for(unsigned b=0;b<32;b++)for(unsigned s=0;s<32;s++)
        ssx_dsp_write(gp->mmio.backend,'X',0x1400+b*32+s,float_to_24b(bins[b][s]));
    bool ep_enabled=ssx_dsp_mmio_enabled(&ep->mmio);
    if(!ep_enabled && !ssx_dsp_mmio_enabled(&gp->mmio) &&
       d->monitor.point!=MCPX_APU_DEBUG_MON_VP)
        memset(d->monitor.frame_buf+(d->ep_frame_div%8)*32,0,32*sizeof d->monitor.frame_buf[0]);
    if(ssx_dsp_mmio_enabled(&gp->mmio) && run(gp) && !ep_enabled &&
       d->monitor.point!=MCPX_APU_DEBUG_MON_VP) {
        unsigned off=(d->ep_frame_div%8)*32;
        for(unsigned i=0;i<32;i++)for(unsigned c=0;c<2;c++)
            d->monitor.frame_buf[off+i][c]=(int16_t)(ssx_dsp_read(gp->mmio.backend,'X',0x1400+c*32+i)>>8);
    }
    if(!runtime->failed && ep_enabled && d->ep_frame_div%8==0)run(ep);
    if(runtime->failed)memset(d->monitor.frame_buf,0,sizeof d->monitor.frame_buf);
}
void ssx_apu_install_dsp_backend(void) {
    static const MCPXDspBackend backend={initialize,destroy,reset,frame,read_register,write_register};
    mcpx_apu_set_dsp_backend(&backend);
}
