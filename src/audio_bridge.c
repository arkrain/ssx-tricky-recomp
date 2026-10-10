/* Explicit generated-code MMIO avoids host-specific fault decoding. */
#include "apu.h"
#include "xbox_memory_layout.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
extern uintptr_t g_xbox_mem_offset;
static MCPXAPUState *apu;
static int trace;
#ifdef SSX_HAVE_DSP
/* Exported build policy lets the packager distinguish older opt-in binaries. */
const unsigned ssx_apu_default_dsp = 1;
#endif
int ssx_apu_init(void) {
    trace=getenv("RECOMP_APU_TRACE")!=NULL;
    const char *dsp=getenv("SSX_APU_DSP");
    if(dsp && strcmp(dsp,"0") && strcmp(dsp,"1")) {
        fprintf(stderr,"[APU] SSX_APU_DSP must be 0 or 1\n");return 0;
    }
    int use_dsp=dsp && !strcmp(dsp,"1");
#ifdef SSX_HAVE_DSP
    if(!dsp)use_dsp=ssx_apu_default_dsp;
#endif
    mcpx_apu_set_dsp_backend(NULL);
    if(use_dsp) {
#ifdef SSX_HAVE_DSP
        extern void ssx_apu_install_dsp_backend(void);
        ssx_apu_install_dsp_backend();
#else
        fprintf(stderr,"[APU] This build lacks the optional DSP interpreter\n");return 0;
#endif
    }
    mcpx_apu_set_low_ram((uint8_t *)g_xbox_mem_offset, xbox_GetMappedSize());
    extern int ssx_audio_source_ready(void);
    mcpx_apu_set_source_pacing(ssx_audio_source_ready);
    apu=mcpx_apu_init_standalone((uint8_t *)(g_xbox_mem_offset+0x80000000u));
    return apu!=NULL;
}
void ssx_apu_shutdown(void) { if(apu)mcpx_apu_shutdown(apu); apu=NULL; mcpx_apu_set_source_pacing(NULL); }
int ssx_apu_read32(uint32_t address, uint32_t *value) {
    if(!apu || (address&3))return 0;
    *value=(uint32_t)mcpx_apu_mmio_read(apu,address-0xfe800000u,4);
    static unsigned count;
    if(trace && count++<400)fprintf(stderr,"[APUMMIO] read %08x = %08x\n",address,*value);
    return 1;
}
int ssx_apu_write32(uint32_t address, uint32_t value) {
    if(!apu || (address&3))return 0;
    static unsigned count;
    if(trace && count++<400)fprintf(stderr,"[APUMMIO] write %08x = %08x\n",address,value);
    mcpx_apu_mmio_write(apu,address-0xfe800000u,value,4);
    return 1;
}
#include "ac97.h"
int ssx_ac97_write8(uint32_t address, uint8_t value) {
    if(!apu)return 0;
    return ssx_ac97_store8((volatile uint8_t *)(g_xbox_mem_offset+0xfec00100u),
                          address-0xfec00100u,value);
}
