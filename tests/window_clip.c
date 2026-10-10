#define xbox_GetMemoryOffset window_test_memory_offset
#include "../third_party/xboxrecomp/src/kernel/nv2a_pb_exec.c"
#undef xbox_GetMemoryOffset
#include <assert.h>
#include "window_clip_cases.h"
static ptrdiff_t test_memory;
ptrdiff_t window_test_memory_offset(void){return test_memory;}
typedef void (*recomp_func_t)(void);
recomp_func_t recomp_lookup(uint32_t va){(void)va;return NULL;}
extern ptrdiff_t g_xbox_mem_offset;
int main(void) {
    unsigned char *ram=calloc(1,4*1024*1024);assert(ram);
    test_memory=g_xbox_mem_offset=(ptrdiff_t)(uintptr_t)ram;
    s_gpu.color_offset=0x300000;s_gpu.depth_offset=0x310000;
    s_gpu.pitch=s_gpu.depth_pitch=64;s_gpu.clip_w=s_gpu.clip_h=16;
    s_surface=ram+dma_resolve(s_gpu.color_offset);
    uint32_t *color=(uint32_t *)s_surface,*depth=(uint32_t *)(ram+dma_resolve(s_gpu.depth_offset));
    s_gpu.depth_enable=s_gpu.depth_write=1;s_gpu.depth_func=0x201;
    s_color_write_mask=0xffffffff;s_zpass_enabled=1;
    /* Ascending method writes program independent slots; a later low-slot
     * write replaces the suffix, including values set earlier at high slots. */
    for(unsigned axis=0;axis<2;axis++)for(unsigned slot=0;slot<8;slot++) {
        unsigned base=axis?0x2e0:0x2c0;
        for(unsigned i=0;i<8;i++)nv2a_pb_exec_method(0,base+i*4,0x00100000+i);
        nv2a_pb_exec_method(0,base+slot*4,0x00090003);
        for(unsigned i=0;i<8;i++)assert(s_render_reg[base/4+i]==(i<slot?0x00100000+i:0x00090003));
    }
    for(unsigned c=0;c<12;c++)for(unsigned mode=0;mode<2;mode++) {
        uint32_t regs[2048];unsigned char expected[256];window_case(c,regs,expected);
        for(unsigned i=0;i<8;i++) {
            nv2a_pb_exec_method(0,0x2c0+i*4,regs[0x2c0/4+i]);
            nv2a_pb_exec_method(0,0x2e0+i*4,regs[0x2e0/4+i]);
        }
        nv2a_pb_exec_method(0,0x2b4,mode);s_zpass_count=0;unsigned count=0;
        for(unsigned i=0;i<256;i++){color[i]=0xff112233;depth[i]=0xffffff5a;}
        for(unsigned y=0;y<16;y++)for(unsigned x=0;x<16;x++)fragment(ram,4,x,y,50,0xff00ff00);
        for(unsigned i=0;i<256;i++) {
            int keep=expected[i]!=mode;count+=keep;
            assert(color[i]==(keep?0xff00ff00:0xff112233));
            assert(depth[i]==(keep?0x325a:0xffffff5a));
        }
        assert(s_zpass_count==count);
    }
    free(ram);puts("24 CPU window cases: colour, depth and zpass passed");return 0;
}
