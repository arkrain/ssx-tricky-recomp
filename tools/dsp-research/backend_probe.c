/* Adapter/DMA integration probe; not a game-audio correctness claim. */
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "dsp.h"
#include "dsp_dma_regs.h"
#include "apu_dsp_backend.h"
#include "apu_dsp_mmio.h"
#include "apu_dsp_dma.h"
const DSPOps jit_dsp_ops={0};
void dsp_jit_init(DSPState *d) {(void)d;abort();}
static unsigned char scratch[32768],fifo[64];
static size_t fifo_size;
static unsigned scratch_reads;
static unsigned char guest_ram[32768];
static void guest_scratch_rw(void *opaque,uint8_t *p,uint32_t address,size_t length,bool write) {
    assert(opaque==guest_ram);
    assert(ssx_dsp_sg_transfer(guest_ram,sizeof guest_ram,0,1,p,address,length,write));
}
static void scratch_rw(void *opaque,uint8_t *p,uint32_t addr,size_t len,bool write) {
    assert(opaque==scratch && addr<=sizeof scratch && len<=sizeof scratch-addr);
    if(write)memcpy(scratch+addr,p,len);else {scratch_reads++;memcpy(p,scratch+addr,len);}
}
static void fifo_rw(void *opaque,uint8_t *p,unsigned index,size_t len,bool write) {
    assert(opaque==scratch && index==2 && write && len<=sizeof fifo);
    memcpy(fifo,p,len);fifo_size=len;
}
static void transfer(DSPState *d,unsigned control,unsigned count,unsigned addr,
                     unsigned off,unsigned base,unsigned size) {
    uint32_t node[]={NODE_POINTER_EOL,control,count,addr,off,base,size-1};
    for(unsigned i=0;i<7;i++)dsp_write_memory(d,'X',0x20+i,node[i]);
    dsp_dma_write(&d->dma,DMA_NEXT_BLOCK,0x20);
    dsp_dma_write(&d->dma,DMA_CONTROL,DMA_CONTROL_ACTION_START);
    assert(d->dma.eol);
    assert(dsp_dma_read(&d->dma,DMA_CONTROL)&DMA_CONTROL_RUNNING);
    assert(dsp_dma_read(&d->dma,DMA_CONTROL)&DMA_CONTROL_RUNNING);
    assert(dsp_dma_read(&d->dma,DMA_CONTROL)&DMA_CONTROL_STOPPED);
}
int main(void) {
    DSPState *d=dsp_init(scratch,scratch_rw,fifo_rw,true);assert(d);
    dsp_bootstrap(d);dsp_set_halt_requested(d,false);dsp_set_cycle_count(d,0);
    dsp_run(d,8);assert(dsp_get_cycle_count(d)==8);
    dsp_sync_to_vm(d);assert(d->core.pc==4);
    uint32_t words[]={0xaa123456,0xbb654321,0xccabcdef,0xdd765432};
    memcpy(scratch+0x3000,words,sizeof words);
    transfer(d,(6<<10)|(15<<5),4,0x100,0,0x3000,16);
    for(unsigned i=0;i<4;i++)assert(dsp_read_memory(d,'X',0x100+i)==(words[i]&0xffffff));
    transfer(d,(1<<10)|(2<<5)|NODE_CONTROL_DIRECTION,4,0x100,0,0,16);
    assert(fifo_size==8);
    for(unsigned i=0;i<4;i++) {
        uint16_t sample;memcpy(&sample,fifo+i*2,2);assert(sample==(uint16_t)(words[i]>>8));
    }
    transfer(d,(6<<10)|(14<<5)|NODE_CONTROL_DIRECTION|(1<<4),2,0x100,6,0x3100,8);
    unsigned char expected[8];uint32_t lo=words[0]&0xffffff,hi=words[1]&0xffffff;
    memcpy(expected,&lo,4);memcpy(expected+4,&hi,4);
    for(unsigned i=0;i<8;i++)assert(scratch[0x3100+(6+i)%8]==expected[i]);
    assert(dsp_read_memory(d,'X',0x24)==6);
    /* Read the wrapped data into another DSP memory space. */
    transfer(d,(6<<10)|(14<<5),2,0x1800+0x100,6,0x3100,8);
    assert(dsp_read_memory(d,'Y',0x100)==lo);
    assert(dsp_read_memory(d,'Y',0x101)==hi);
    /* Two planar channels, two samples each, become interleaved FIFO PCM. */
    transfer(d,(1<<10)|(2<<5)|NODE_CONTROL_DIRECTION|1,(2<<4)|1,0x100,0,0,16);
    assert(fifo_size==8);
    const unsigned order[]={0,2,1,3};
    for(unsigned i=0;i<4;i++) {
        uint16_t sample;memcpy(&sample,fifo+i*2,2);
        assert(sample==(uint16_t)(words[order[i]]>>8));
    }
    dsp_start_frame(d);assert(d->interrupts&2);
    dsp_write_memory(d,'X',0xffffc5,2);assert(!(d->interrupts&2));
    dsp_write_memory(d,'X',0xffffc4,1);assert(dsp_get_halt_requested(d));
    dsp_reset(d);dsp_sync_to_vm(d);assert(d->core.pc==0);
    dsp_destroy(d);
    SsxDsp *wrapped=ssx_dsp_create(scratch,scratch_rw,fifo_rw,true);assert(wrapped);
    ssx_dsp_bootstrap(wrapped);
    ssx_dsp_write(wrapped,'Y',13,0x2468);
    assert(ssx_dsp_read(wrapped,'Y',13)==0x2468);
    unsigned cycles=0;
    assert(!ssx_dsp_frame(wrapped,20,&cycles) && cycles==20);
    ssx_dsp_reset(wrapped);
    /* MOVEP #1,X:$ffffc4 requests a halt through the real peripheral path. */
    ssx_dsp_write(wrapped,'P',0,0x08f484);
    ssx_dsp_write(wrapped,'P',1,1);
    assert(ssx_dsp_frame(wrapped,20,&cycles) && cycles>0 && cycles<=20);
    /* Unspent cycles from a halt must not enlarge the next frame's budget. */
    assert(!ssx_dsp_frame(wrapped,20,&cycles) && cycles==20);
    ssx_dsp_destroy(wrapped);
    for(unsigned gp=0;gp<2;gp++) {
        wrapped=ssx_dsp_create(scratch,scratch_rw,fifo_rw,gp!=0);
        SsxDspMmio *m=malloc(sizeof *m);assert(m);
        ssx_dsp_mmio_init(m,wrapped,gp!=0);uint32_t value=0;
        const uint32_t offsets[]={0,(gp?0x4000:0x3000)-4,0x6000,
            (gp?0x8000:0x6400)-4,0xa000,0xdffc};
        for(unsigned i=0;i<sizeof offsets/sizeof offsets[0];i++) {
            assert(ssx_dsp_mmio_write(m,offsets[i],4,0xff123400+i));
            assert(ssx_dsp_mmio_read(m,offsets[i],4,&value) && value==0x123400+i);
        }
        if(gp) {
            assert(ssx_dsp_mmio_write(m,0x5000,4,0x654321));
            assert(ssx_dsp_mmio_read(m,0x3000,4,&value) && value==0x654321);
        }
        assert(ssx_dsp_mmio_write(m,0x9000,4,0xfedcba98));
        assert(ssx_dsp_mmio_read(m,0x9000,4,&value) && value==0xfedcba98);
        assert(!ssx_dsp_mmio_write(m,1,4,1));
        assert(!ssx_dsp_mmio_write(m,0x10000,4,1));
        assert(!ssx_dsp_mmio_read(m,0,2,&value));
        unsigned before=scratch_reads;
        assert(!ssx_dsp_mmio_enabled(m));
        assert(ssx_dsp_mmio_write(m,0xfffc,4,3) && ssx_dsp_mmio_enabled(m));
        assert(scratch_reads==before+1);
        assert(ssx_dsp_mmio_write(m,0xfffc,4,3) && scratch_reads==before+1);
        assert(ssx_dsp_mmio_write(m,0xfffc,4,1) && !ssx_dsp_mmio_enabled(m));
        assert(ssx_dsp_mmio_write(m,0xfffc,4,3) && scratch_reads==before+2);
        ssx_dsp_destroy(wrapped);free(m);
    }
    /* APU-visible reset loads a program through physical scatter/gather RAM. */
    guest_ram[1]=0x10;guest_ram[9]=0x30; /* Entries map pages at0x1000,0x3000. */
    guest_ram[0x1000]=0x84;guest_ram[0x1001]=0xf4;guest_ram[0x1002]=8;
    guest_ram[0x1003]=0xff;guest_ram[0x1004]=1;
    wrapped=ssx_dsp_create(guest_ram,guest_scratch_rw,fifo_rw,true);
    SsxDspMmio *mapped=malloc(sizeof *mapped);assert(mapped);
    ssx_dsp_mmio_init(mapped,wrapped,true);
    assert(ssx_dsp_mmio_write(mapped,0xfffc,4,3));
    uint32_t instruction=0;
    assert(ssx_dsp_mmio_read(mapped,0xa000,4,&instruction) && instruction==0x08f484);
    assert(ssx_dsp_frame(wrapped,20,&cycles));
    ssx_dsp_destroy(wrapped);free(mapped);
    puts("DSP backend/DMA/MMIO and guest scatter/gather bootstrap-to-halt passed");
    return 0;
}
