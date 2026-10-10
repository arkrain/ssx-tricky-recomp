/* C-interpreter adapter shared by the runtime and standalone probe. */
#include "apu_dsp_backend.h"
#include "dsp.h"
SsxDsp *ssx_dsp_create(void *opaque,SsxDspScratch scratch,SsxDspFifo fifo,bool gp) {
    return (SsxDsp *)dsp_init(opaque,scratch,fifo,gp);
}
void ssx_dsp_destroy(SsxDsp *d) {dsp_destroy((DSPState *)d);}
void ssx_dsp_reset(SsxDsp *d) {dsp_reset((DSPState *)d);}
void ssx_dsp_bootstrap(SsxDsp *d) {dsp_bootstrap((DSPState *)d);}
uint32_t ssx_dsp_read(SsxDsp *d,char space,uint32_t address) {
    return dsp_read_memory((DSPState *)d,space,address);
}
void ssx_dsp_write(SsxDsp *d,char space,uint32_t address,uint32_t value) {
    dsp_write_memory((DSPState *)d,space,address,value);
}
bool ssx_dsp_frame(SsxDsp *d,unsigned budget,unsigned *cycles) {
    DSPState *backend=(DSPState *)d;
    /* dsp_run retains unused credits when a program halts. Each audio frame
     * gets a fresh diagnostic budget, so prior credits must not extend it. */
    backend->save_cycles=0;
    dsp_start_frame(backend);dsp_set_halt_requested(backend,false);
    dsp_set_cycle_count(backend,0);
    unsigned used=0;
    while(used<budget && !dsp_get_halt_requested(backend)) {
        unsigned slice=budget-used;if(slice>1000)slice=1000;
        dsp_run(backend,(int)slice);used=dsp_get_cycle_count(backend);
    }
    if(cycles)*cycles=used;
    return dsp_get_halt_requested(backend);
}
