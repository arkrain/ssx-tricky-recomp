#include "apu_dsp_mmio.h"
#include <string.h>
static char window(bool gp,uint32_t offset,uint32_t *address) {
    if(offset<(gp?0x4000u:0x3000u)) {*address=offset/4;return 'X';}
    if(gp && offset>=0x5000 && offset<0x6000) {*address=0x1400+(offset-0x5000)/4;return 'X';}
    if(offset>=0x6000 && offset<(gp?0x8000u:0x6400u)) {*address=(offset-0x6000)/4;return 'Y';}
    if(offset>=0xa000 && offset<0xe000) {*address=(offset-0xa000)/4;return 'P';}
    return 0;
}
void ssx_dsp_mmio_init(SsxDspMmio *m,SsxDsp *backend,bool gp) {
    memset(m,0,sizeof *m);m->backend=backend;m->gp=gp;
}
bool ssx_dsp_mmio_enabled(const SsxDspMmio *m) {
    return (m->registers[0xfffc/4]&3)==3;
}
static bool valid(SsxDspMmio *m,uint32_t offset,unsigned size) {
    return m && m->backend && size==4 && offset<0x10000 && !(offset&3);
}
bool ssx_dsp_mmio_read(SsxDspMmio *m,uint32_t offset,unsigned size,uint32_t *value) {
    if(!value || !valid(m,offset,size))return false;
    uint32_t address;char space=window(m->gp,offset,&address);
    *value=space?ssx_dsp_read(m->backend,space,address):m->registers[offset/4];
    return true;
}
bool ssx_dsp_mmio_write(SsxDspMmio *m,uint32_t offset,unsigned size,uint32_t value) {
    if(!valid(m,offset,size))return false;
    uint32_t address;char space=window(m->gp,offset,&address);
    if(space)ssx_dsp_write(m->backend,space,address,value&0xffffff);
    else {
        if(offset==0xfffc) {
            if((value&3)!=3)ssx_dsp_reset(m->backend);
            else if(!ssx_dsp_mmio_enabled(m))ssx_dsp_bootstrap(m->backend);
        }
        m->registers[offset/4]=value;
    }
    return true;
}
