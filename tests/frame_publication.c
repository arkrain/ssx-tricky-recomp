/* Exercise the real command path: publish only at the submitted flip. */
#define xbox_GetMemoryOffset publication_memory_offset
#define xbox_FramebufferWindowSet publication_capture
#define xbox_FramebufferWindowStart publication_window_start
#ifndef SSX_PB_SOURCE
#define SSX_PB_SOURCE "../third_party/xboxrecomp/src/kernel/nv2a_pb_exec.c"
#endif
#include SSX_PB_SOURCE
#undef xbox_GetMemoryOffset
#undef xbox_FramebufferWindowSet
#undef xbox_FramebufferWindowStart
#include <assert.h>
static ptrdiff_t test_memory;
static unsigned publications;
static uint32_t published_address,published_pitch,published_pixels[640*480];
ptrdiff_t publication_memory_offset(void) { return test_memory; }
void publication_window_start(void) {}
void publication_capture(uint32_t address,uint32_t pitch) {
    publications++;published_address=address;published_pitch=pitch;
    assert(pitch==2560 || pitch==1280);
    memcpy(published_pixels,(void *)(test_memory+address),pitch*480);
}
typedef void (*recomp_func_t)(void);
recomp_func_t recomp_lookup(uint32_t va) { (void)va;return NULL; }
extern ptrdiff_t g_xbox_mem_offset;
static void target(uint32_t offset,uint32_t pitch) {
    nv2a_pb_exec_method(0,0x0200,640u<<16);
    nv2a_pb_exec_method(0,0x0204,480u<<16);
    nv2a_pb_exec_method(0,0x020c,pitch);
    nv2a_pb_exec_method(0,0x0210,offset);
}
static void clear(uint32_t color,unsigned top,unsigned bottom) {
    nv2a_pb_exec_method(0,0x1d98,639u<<16);
    nv2a_pb_exec_method(0,0x1d9c,((bottom-1)<<16)|top);
    nv2a_pb_exec_method(0,0x1d90,color);
    nv2a_pb_exec_method(0,0x1d94,0xf0);
}
static void flip(void) {
    nv2a_pb_exec_method(0,NV097_FLIP_INCREMENT_WRITE,0);
    nv2a_pb_exec_method(0,NV097_FLIP_STALL,0);
}
int main(void) {
    unsigned char *ram=calloc(1,8*1024*1024);assert(ram);
    test_memory=g_xbox_mem_offset=(ptrdiff_t)ram;
    /* No valid target during startup must not publish a stale draw. */
    s_gpu.drawn_offset=0x500000;flip();assert(publications==0);
    target(0x100000,2560);clear(0xff123456,0,480);
    assert(publications==0); /* Old code exposes the pre-clear frame here. */
    clear(0xff000000,0,80);clear(0xff000000,400,480);
    assert(publications==0);
    flip();assert(publications==1 && published_address==dma_resolve(0x100000));
    assert(published_pitch==2560);
    for(unsigned y=0;y<480;y++)for(unsigned x=0;x<640;x++)
        assert(published_pixels[y*640+x]==(y<80 || y>=400?0xff000000:0xff123456));
    /* Beginning the next frame cannot overwrite the completed snapshot. */
    target(0x300000,2560);clear(0xffabcdef,0,480);
    assert(publications==1 && published_pixels[80*640]==0xff123456);
    /* An off-screen geometry draw is not the submitted display target. */
    s_gpu.drawn_offset=0x500000;
    flip();assert(publications==2 && published_address==dma_resolve(0x300000));
    assert(published_pixels[80*640]==0xffabcdef);
    /* A clear-only frame and its new pixel format use the bound target/pitch. */
    target(0x100000,1280);clear(0xffff0000,0,480);
    flip();assert(publications==3 && published_address==dma_resolve(0x100000));
    assert(published_pitch==1280);
    for(unsigned i=0;i<640*480;i++)assert(((uint16_t *)published_pixels)[i]==0xf800);
    /* Invalid/absent target cannot reuse the old geometry address. */
    target(0,2560);flip();assert(publications==3);
    puts("publication: cinematic bars, next target, clear-only and RGB565 passed");
    free(ram);return 0;
}
