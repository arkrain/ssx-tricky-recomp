#include "apu_dsp_dma.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static unsigned char ram[16384],buffer[8192];
static unsigned char low_ram[8192];
static uint8_t *resolve_split(void *opaque,uint64_t address,size_t length) {
    (void)opaque;
    if(address>=0x40000000 && address<0x40000000+sizeof low_ram) {
        size_t offset=(size_t)(address-0x40000000);
        return length<=sizeof low_ram-offset?low_ram+offset:NULL;
    }
    return address<=sizeof ram && length<=sizeof ram-address?ram+(size_t)address:NULL;
}
static void entry(unsigned n,uint32_t address) {
    for(unsigned k=0;k<4;k++)ram[n*8+k]=(uint8_t)(address>>(8*k));
}
int main(void) {
    entry(0,4096);entry(1,12288);
    memset(ram+4096,0x12,4096);memset(ram+12288,0x34,4096);
    assert(ssx_dsp_sg_transfer(ram,sizeof ram,0,1,buffer,4094,6,false));
    assert(buffer[0]==0x12 && buffer[1]==0x12);
    for(unsigned i=2;i<6;i++)assert(buffer[i]==0x34);
    memset(buffer,0x56,6);
    assert(ssx_dsp_sg_transfer(ram,sizeof ram,0,1,buffer,4094,6,true));
    assert(ram[8190]==0x56 && ram[12291]==0x56);
    assert(ssx_dsp_sg_transfer(ram,sizeof ram,0,1,buffer,8191,1,false));
    assert(buffer[0]==0x34); /* Last byte of RAM is valid. */
    memset(buffer,0xab,sizeof buffer);entry(1,0xfffff000);
    assert(!ssx_dsp_sg_transfer(ram,sizeof ram,0,1,buffer,4094,6,false));
    assert(buffer[0]==0xab); /* No partial read before a bad second page. */
    assert(!ssx_dsp_sg_transfer(ram,sizeof ram,0,1,buffer,4094,6,true));
    assert(ram[8190]==0x56);
    entry(1,12288);
    assert(!ssx_dsp_sg_transfer(ram,sizeof ram,0,0,buffer,4094,6,true));
    assert(!ssx_dsp_sg_transfer(ram,sizeof ram,UINT32_MAX,1,buffer,0,4,false));
    assert(!ssx_dsp_sg_transfer(ram,sizeof ram,0,UINT32_MAX,buffer,UINT32_MAX,4,false));
    for(unsigned i=0;i<12;i++)buffer[i]=(uint8_t)i;
    uint32_t cursor=4094;
    assert(ssx_dsp_ring_transfer(ram,sizeof ram,0,1,buffer,4092,4100,&cursor,12,true));
    assert(cursor==4098);
    const unsigned char expected[]={8,9,10,11,4,5,6,7};
    unsigned char readback[8];cursor=4094;
    assert(ssx_dsp_ring_transfer(ram,sizeof ram,0,1,readback,4092,4100,&cursor,8,false));
    assert(!memcmp(readback,expected,8));
    cursor=4094;entry(1,UINT32_MAX);memset(buffer,0xcc,12);
    assert(!ssx_dsp_ring_transfer(ram,sizeof ram,0,1,buffer,4092,4100,&cursor,12,true));
    assert(cursor==4094 && ram[8190]==8);
    assert(!ssx_dsp_ring_transfer(ram,sizeof ram,0,1,buffer,4092,4092,&cursor,1,true));
    /* A transfer can replace the table while writing its first page. */
    entry(0,0);entry(1,12288);memset(buffer,0x77,sizeof buffer);
    assert(ssx_dsp_sg_transfer(ram,sizeof ram,0,1,buffer,0,sizeof buffer,true));
    assert(ram[0]==0x77 && ram[12288]==0x77 && ram[16383]==0x77);
    /* Repeated wraps grow the segment plan beyond its stack storage. */
    entry(0,4096);entry(1,12288);cursor=4092;
    for(unsigned i=0;i<200;i++)buffer[i]=(uint8_t)i;
    assert(ssx_dsp_ring_transfer(ram,sizeof ram,0,1,buffer,4092,4100,&cursor,200,true));
    assert(cursor==4092);
    assert(ssx_dsp_ring_transfer(ram,sizeof ram,0,1,readback,4092,4100,&cursor,8,false));
    for(unsigned i=0;i<8;i++)assert(readback[i]==192+i);
    /* Descriptor tables and their pages may use disjoint APU bus windows. */
    low_ram[1]=0x10; /* Page0 -> physical0x1000. */
    low_ram[9]=0x10;low_ram[11]=0x40; /* Page1 -> ordinary RAM bus0x40001000. */
    memset(ram+4096,0x29,4096);memset(low_ram+4096,0x83,4096);
    assert(ssx_dsp_sg_bus(resolve_split,NULL,0x40000000,1,buffer,4094,6,false));
    assert(buffer[0]==0x29 && buffer[1]==0x29);
    for(unsigned i=2;i<6;i++)assert(buffer[i]==0x83);
    puts("DSP scatter/gather and ring transport bounds passed");return 0;
}
