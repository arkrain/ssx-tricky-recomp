/* Standalone integration smoke check, not a DSP conformance test. */
#include <assert.h>
#include <stdlib.h>
#include "dsp_cpu.h"
int main(void) {
    dsp_core_t *d=calloc(1,sizeof *d);assert(d);
    dsp56k_reset_cpu(d);
    dsp56k_write_memory(d,DSP_SPACE_X,17,0x123456);
    dsp56k_write_memory(d,DSP_SPACE_Y,23,0x654321);
    assert(dsp56k_read_memory(d,DSP_SPACE_X,17)==0x123456);
    assert(dsp56k_read_memory(d,DSP_SPACE_Y,23)==0x654321);
    d->pc=0;
    for(unsigned i=0;i<16;i++)dsp56k_write_memory(d,DSP_SPACE_P,i,0);
    for(unsigned i=0;i<16;i++) {
        dsp56k_execute_instruction(d);assert(d->pc==i+1);
    }
    printf("DSP interpreter standalone memory/NOP smoke passed; core=%zu bytes\n",sizeof *d);
    free(d);return 0;
}
