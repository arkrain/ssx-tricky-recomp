/* Offline diagnostic: replay one GPU batch captured by scripts/capture-draw.py.
 * Capture files contain the user's original assets; keep them local. */
#define xbox_FramebufferWindowSet replay_framebuffer_set
#define xbox_GetMemoryOffset replay_memory_offset
#define xbox_ContiguousAllocatedBytes replay_contiguous_size
#include "../third_party/xboxrecomp/src/kernel/nv2a_pb_exec.c"
#undef xbox_FramebufferWindowSet
#undef xbox_GetMemoryOffset
#undef xbox_ContiguousAllocatedBytes
#include "../src/nv2a_vertex.c"
#include <sys/mman.h>
#include <stddef.h>
#include <time.h>
static ptrdiff_t replay_memory;
void replay_framebuffer_set(uint32_t va,uint32_t pitch) { (void)va;(void)pitch; }
ptrdiff_t replay_memory_offset(void) { return replay_memory; }
uint32_t replay_contiguous_size(void) { return 0x04000000; }
typedef void (*recomp_func_t)(void);
recomp_func_t recomp_lookup(uint32_t va) { (void)va; return NULL; }
static void read_blob(const char *dir,const char *name,void *dst,size_t size) {
    char path[1024];snprintf(path,sizeof path,"%s/%s.bin",dir,name);
    FILE *f=fopen(path,"rb");
    if(!f || fread(dst,1,size,f)!=size) { fprintf(stderr,"cannot read %s (%zu bytes)\n",path,size);exit(1); }
    fclose(f);
}
int main(int argc,char **argv) {
    if(argc!=2){fprintf(stderr,"usage: %s capture-directory\n",argv[0]);return 2;}
    void *ram=mmap(NULL,UINT64_C(0x100000000),PROT_READ|PROT_WRITE,MAP_PRIVATE|MAP_ANONYMOUS|MAP_NORESERVE,-1,0);
    if(ram==MAP_FAILED){perror("mmap");return 1;}replay_memory=(ptrdiff_t)ram;
    read_blob(argv[1],"physical",(char*)ram+0x80000000u,0x4000000);
#define LOAD(name) read_blob(argv[1],#name,&name,sizeof name)
    LOAD(s_gpu);LOAD(s_render_reg);LOAD(s_tex_reg);LOAD(s_tex_set);LOAD(s_color_write_mask);
    LOAD(words);LOAD(constants);LOAD(start);LOAD(mode);
    for(int i=0;i<136;i++) {
        valid[i]=nv2a_vsh_parse_step(&steps[i],words[i])==NV2AVPR_SUCCESS;
        if(valid[i] && steps[i].mac.opcode==NV2AOP_ARL)
            steps[i].mac.outputs[0].writemask=NV2AWM_X;
    }
    s_render_every=1;s_skip_frame=0;
    fprintf(stderr,"primitive=%u vertices=%u clip=%u,%u %ux%u surface=%08x depth=%08x shader=%u\n",s_gpu.prim,s_gpu.idx_count,s_gpu.clip_x,s_gpu.clip_y,s_gpu.clip_w,s_gpu.clip_h,s_gpu.color_offset,s_gpu.depth_offset,mode);
    for(int i=0;i<16;i++)fprintf(stderr,"attribute%d offset=%08x type=%u size=%u stride=%u\n",i,s_gpu.attr[i].offset,s_gpu.attr[i].type,s_gpu.attr[i].size,s_gpu.attr[i].stride);
    const char *opts=getenv("SSX_REPLAY_OPTIONS");
    if(opts && strstr(opts,"nonfinite-constant"))constants[0]=NAN;
    if(opts && strstr(opts,"nonfinite-input")) {
        s_gpu.attr[0].size=0;s_gpu.attr[0].stride=0;s_gpu.imm_attr[0][0]=NAN;
    }
    if(opts && strstr(opts,"no-cull"))s_render_reg[0x308/4]=0;
    if(opts && strstr(opts,"no-depth"))s_gpu.depth_enable=0;
    if(opts && strstr(opts,"flat")){s_render_reg[0x1e60/4]=0;s_gpu.tex.valid=0;}
    if(opts && strstr(opts,"clear")) {
        uint32_t saved=s_gpu.clear_color;s_gpu.clear_color=0xff202020;
        clear_surface(0xf1);s_gpu.clear_color=saved;
    }
    prepare_textures();
    for(unsigned i=0;i<s_gpu.idx_count && i<12;i++) {
        float in[16][4],out[13][4]={{0}};
        for(int a=0;a<16;a++)if(!fetch_attr(&s_gpu.attr[a],s_gpu.idx[i],in[a]))memcpy(in[a],s_gpu.imm_attr[a],16);
        int ok=ssx_vertex_execute(in,out);
        fprintf(stderr,"valid=%d vertex%u index=%u input=%g,%g,%g,%g output=%g,%g,%g,%g\n",ok,i,s_gpu.idx[i],in[0][0],in[0][1],in[0][2],in[0][3],out[0][0],out[0][1],out[0][2],out[0][3]);
    }
    ssx_gl_sync();
    unsigned height=s_gpu.clip_y+s_gpu.clip_h;
    size_t color_bytes=(size_t)s_gpu.pitch*height;
    size_t depth_bytes=s_gpu.depth_offset?(size_t)s_gpu.depth_pitch*height:0;
    void *color=(char*)ram+dma_resolve(s_gpu.color_offset),*depth=(char*)ram+dma_resolve(s_gpu.depth_offset);
    const char *repeat_env=getenv("SSX_REPLAY_RUNS"),*warm_env=getenv("SSX_REPLAY_WARMUP");
    unsigned repeats=repeat_env?strtoul(repeat_env,NULL,10):1,warmup=warm_env?strtoul(warm_env,NULL,10):0;
    if(!repeats || repeats>1000 || warmup>10000)return 2;
    void *initial=malloc(color_bytes),*initial_depth=malloc(depth_bytes?depth_bytes:1);
    if(!initial || !initial_depth)return 1;
    memcpy(initial,color,color_bytes);if(depth_bytes)memcpy(initial_depth,depth,depth_bytes);
    extern uint64_t ssx_gpu_geometry_batches,ssx_gpu_geometry_fallbacks;
    s_drawn_dumps=FB_DUMP_AFTER_DRAW; /* Exclude diagnostic BMP writes from timings. */
    for(unsigned run=0;run<repeats+warmup;run++) {
        memcpy(color,initial,color_bytes);if(depth_bytes)memcpy(depth,initial_depth,depth_bytes);ssx_gl_invalidate();
        struct timespec begin,submitted,done;clock_gettime(CLOCK_MONOTONIC,&begin);
        raster_batch();clock_gettime(CLOCK_MONOTONIC,&submitted);ssx_gl_sync();clock_gettime(CLOCK_MONOTONIC,&done);
        if(run>=warmup)printf("{\"submission_seconds\":%.9g,\"seconds\":%.9g,\"gpu_batches\":%llu,\"gpu_fallbacks\":%llu,\"cpu_retries\":%llu}\n",
            submitted.tv_sec-begin.tv_sec+(submitted.tv_nsec-begin.tv_nsec)*1e-9,
            done.tv_sec-begin.tv_sec+(done.tv_nsec-begin.tv_nsec)*1e-9,
            (unsigned long long)ssx_gpu_geometry_batches,(unsigned long long)ssx_gpu_geometry_fallbacks,
            (unsigned long long)s_geometry_cpu_retries);
    }
    const char *prefix=getenv("SSX_REPLAY_OUTPUT");
    if(prefix) {
        char path[4096];snprintf(path,sizeof path,"%s.color.bin",prefix);FILE *f=fopen(path,"wb");
        if(!f || fwrite(color,1,color_bytes,f)!=color_bytes || fclose(f))return 1;
        snprintf(path,sizeof path,"%s.depth.bin",prefix);f=fopen(path,"wb");
        if(!f || fwrite(depth,1,depth_bytes,f)!=depth_bytes || fclose(f))return 1;
    }
    free(initial);free(initial_depth);
    s_gpu.drawn_offset=s_gpu.color_offset;
    dump_surface_bmp();
    return 0;
}
