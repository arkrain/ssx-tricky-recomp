#ifndef SSX_AC97_H
#define SSX_AC97_H
#include <stdint.h>
/* AC97 bus-master register reset. Control interrupt enables survive reset;
 * descriptors, indices and count clear, and status becomes DMA halted. */
static inline int ssx_ac97_store8(volatile uint8_t *regs, unsigned offset, uint8_t value) {
    unsigned block=offset&~15u;
    if(block!=0 && block!=0x10 && block!=0x20 && block!=0x70)return 0;
    if((offset&15u)!=11)return 0;
    if(value&2) {
        uint8_t enables=regs[block+11]&0x1c;
        for(unsigned i=0;i<12;i++)regs[block+i]=0;
        regs[block+6]=1;
        regs[block+11]=enables;
    } else {
        regs[offset]=value&0x1f;
        if(!(value&1))regs[block+6]|=1;
    }
    return 1;
}
#endif
