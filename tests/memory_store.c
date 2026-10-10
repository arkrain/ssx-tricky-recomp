/* Exercise RAM/device dispatch in the generated-code memory helper. */
#define RECOMP_DIRECT_USB_MMIO
#define RECOMP_DIRECT_APU_MMIO
#ifndef STORE_HEADER
#define STORE_HEADER "recomp_types.h"
#endif
#include STORE_HEADER
#include <assert.h>
#include <sys/mman.h>
#include <stdio.h>
#include <time.h>

ptrdiff_t g_xbox_mem_offset;
static unsigned usb_calls,apu_calls;
static uint32_t last_address,last_value;
static int consume;
int xbox_OhciWrite32(uint32_t address,uint32_t value) {
    assert((address>=0xfed00000 && address<0xfed01000) ||
           (address>=0xfed08000 && address<0xfed09000));
    usb_calls++;last_address=address;last_value=value;return consume;
}
int ssx_apu_write32(uint32_t address,uint32_t value) {
    assert(address>=0xfe800000 && address<0xfe880000);
    apu_calls++;last_address=address;last_value=value;return consume;
}

int main(int argc,char **argv) {
    (void)argv;
    size_t bytes=(size_t)UINT32_MAX+1;
    void *memory=mmap(NULL,bytes,PROT_READ|PROT_WRITE,
                      MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE,-1,0);
    assert(memory!=MAP_FAILED);g_xbox_mem_offset=(ptrdiff_t)(uintptr_t)memory;
    const uint32_t ram[]={0,0x1000,0x80001000,0xfcfffffc,0xfd000000,
        0xfe7ffffc,0xfe880000,0xfecffffc,0xfed01000,0xfed07ffc,
        0xfed09000,0xfffffffc};
    for(unsigned i=0;i<sizeof ram/sizeof *ram;i++) {
        RECOMP_STORE32(ram[i],0x12340000+i);
        assert(MEM32(ram[i])==0x12340000+i);
    }
    assert(!usb_calls && !apu_calls);
    const uint32_t device[]={0xfe800000,0xfe87fffc,0xfed00000,
        0xfed00ffc,0xfed08000,0xfed08ffc};
    for(unsigned i=0;i<sizeof device/sizeof *device;i++) {
        uint32_t address=device[i],value=0x87650000+i;
        unsigned before=usb_calls+apu_calls;
        MEM32(address)=0xabcdef01;consume=1;
        RECOMP_STORE32(address,value);
        assert(usb_calls+apu_calls==before+1);
        assert(last_address==address && last_value==value);
        assert(MEM32(address)==0xabcdef01);
        consume=0;RECOMP_STORE32(address,value);
        assert(usb_calls+apu_calls==before+2 && MEM32(address)==value);
    }
    MEM32(0xfd600100)=3;MEM32(0xfd000100)=0x1000;
    RECOMP_STORE32(0xfd600140,1);
    assert(MEM32(0xfd000100)==0x1001000);
    RECOMP_STORE32(0xfd600100,1);
    assert(MEM32(0xfd600100)==2 && MEM32(0xfd000100)==0x1000);
    RECOMP_STORE32(0xfd100410,0x12350000);
    assert(MEM32(0xfd100410)==0x12340000);
    if(argc>1) {
        /* Volatile runtime addresses prevent compile-time range folding.
         * Alternate low/high RAM, with a rare ordinary aperture write. */
        volatile uint32_t addresses[4096];
        for(unsigned i=0;i<4096;i++)
            addresses[i]=0x10000+(i&1023)*4+((i&1)?0x80000000:0);
        addresses[4095]=0xfd200000;
        struct timespec start,end;clock_gettime(CLOCK_MONOTONIC,&start);
        for(unsigned i=0;i<64000000;i++)RECOMP_STORE32(addresses[i&4095],i);
        clock_gettime(CLOCK_MONOTONIC,&end);
        uint64_t hash=1469598103934665603ULL;
        for(unsigned i=0;i<4096;i++)hash=(hash^MEM32(addresses[i]))*1099511628211ULL;
        printf("%016llx %.9f\n",(unsigned long long)hash,
            end.tv_sec-start.tv_sec+(end.tv_nsec-start.tv_nsec)*1e-9);
    }
    assert(!munmap(memory,bytes));return 0;
}
