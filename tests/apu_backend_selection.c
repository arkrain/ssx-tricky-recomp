/* Exercise the actual bridge's policy without an audio device or guest RAM. */
#include "../src/audio_bridge.c"
#include <assert.h>
uintptr_t g_xbox_mem_offset=0x100000000ull;
static int installed, initialized;
int ssx_audio_source_ready(void){return 1;}
void mcpx_apu_set_source_pacing(int (*ready)(void)){(void)ready;}
static char dummy_state;
size_t xbox_GetMappedSize(void) { return 0x4000000; }
void mcpx_apu_set_dsp_backend(const MCPXDspBackend *backend) { installed=backend!=NULL; }
void ssx_apu_install_dsp_backend(void) { installed=1; }
void mcpx_apu_set_low_ram(uint8_t *ram,size_t size) {
    assert((uintptr_t)ram==g_xbox_mem_offset && size==0x4000000);
}
MCPXAPUState *mcpx_apu_init_standalone(uint8_t *ram) {
    assert((uintptr_t)ram==g_xbox_mem_offset+0x80000000u);
    initialized++;return (MCPXAPUState *)&dummy_state;
}
void mcpx_apu_shutdown(MCPXAPUState *d) { assert(d==(MCPXAPUState *)&dummy_state); }
uint64_t mcpx_apu_mmio_read(MCPXAPUState *d,uint64_t a,unsigned s) { (void)d;(void)a;(void)s;return 0; }
void mcpx_apu_mmio_write(MCPXAPUState *d,uint64_t a,uint64_t v,unsigned s) { (void)d;(void)a;(void)v;(void)s; }
static void check(const char *option,int success,int expected_backend) {
    if(option)setenv("SSX_APU_DSP",option,1);else unsetenv("SSX_APU_DSP");
    int before=initialized;
    assert(ssx_apu_init()==success);
    assert(initialized==before+success);
    if(success) { assert(installed==expected_backend);ssx_apu_shutdown(); }
}
int main(void) {
#ifdef SSX_HAVE_DSP
    check(NULL,1,1);check("0",1,0);check("1",1,1);
    check("0",1,0); /* Reinitialization must clear the previous backend. */
#else
    check(NULL,1,0);check("0",1,0);check("1",0,0);
#endif
    check("",0,0);check("true",0,0);check("2",0,0);
    unsetenv("SSX_APU_DSP");puts("APU backend selection passed");return 0;
}
