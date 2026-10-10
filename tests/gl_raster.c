/* Compare GL fragment programs to the independent CPU combiner evaluator. */
#include "nv2a_gl.h"
#include "nv2a_combiner.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
static uint32_t seed=0x51ab102;
static uint32_t random_word(void){seed=seed*1664525u+1013904223u;return seed;}
static int channel_difference(uint32_t a,uint32_t b){int largest=0;for(int k=0;k<4;k++){int d=(int)((a>>(k*8))&255)-(int)((b>>(k*8))&255);if(abs(d)>largest)largest=abs(d);}return largest;}
static int sample(const void *p,int x,int y,uint32_t *out){*out=((const uint32_t *)p)[y*2+x];return 1;}
static int sample_mip(const void *p,unsigned level,int x,int y,uint32_t *out){
    unsigned size=4,offset=0;
    for(unsigned i=0;i<level;i++){offset+=size*size;size=size>1?size/2:1;}
    assert(level<3 && x>=0 && y>=0 && (unsigned)x<size && (unsigned)y<size);
    *out=((const uint32_t *)p)[offset+y*size+x];return 1;
}
/* Several dirty surfaces synchronize together. Pitch padding and surrounding
 * memory belong to the caller and must survive both batch and direct paths. */
static void readback_pitches(void) {
    enum {width=19,height=13,stride=27,words=stride*height};
    uint32_t color[2][words+8],z[words+8],r[2048]={0};
    for(unsigned i=0;i<words+8;i++){color[0][i]=color[1][i]=0x55aa33cc;z[i]=0xffffff5a;}
    SsxGlDraw d={0};d.width=d.clip_w=width;d.height=d.clip_h=height;
    d.pitch=d.depth_pitch=stride*4;d.depth=z+4;
    d.depth_enable=d.depth_write=1;d.depth_func=0x201;
    d.color_mask=0xffffffff;d.blend_src=1;d.registers=r;r[0x2c0/4]=r[0x2e0/4]=0x0fff0000;
    SsxGlVertex v[3]={0};
    for(unsigned i=0;i<3;i++) {
        v[i].position[0]=i==1?17:1;v[i].position[1]=i==2?11:1;
        v[i].position[2]=100;v[i].position[3]=1;v[i].diffuse[0]=v[i].diffuse[3]=1;
    }
    /* Populate all three cache slots first: eviction itself legitimately
     * synchronizes dirty surfaces, independently of explicit sync calls. */
    ssx_gl_invalidate();
    for(unsigned i=0;i<2;i++){d.color=color[i]+4;assert(ssx_gl_draw(&d,v,3));}
    ssx_gl_sync();
    for(unsigned i=0;i<words+8;i++){color[0][i]=color[1][i]=0x55aa33cc;z[i]=0xffffff5a;}
    ssx_gl_invalidate();d.color=color[0]+4;assert(ssx_gl_draw(&d,v,3));
    for(unsigned i=0;i<3;i++){v[i].position[2]=50;v[i].diffuse[0]=0;v[i].diffuse[2]=1;}
    d.color=color[1]+4;assert(ssx_gl_draw(&d,v,3));
    /* Cached CPU backing stays unchanged until the explicit synchronization point. */
    assert(color[0][4+2*stride+2]==0x55aa33cc && z[4+2*stride+2]==0xffffff5a);
    ssx_gl_sync();
    assert(color[0][4+2*stride+2]==0xffff0000 && color[1][4+2*stride+2]==0xff0000ff);
    assert(abs((int)(z[4+2*stride+2]>>8)-50)<=1 && (z[4+2*stride+2]&255)==0x5a);
    /* Reuse exactly the same draw framebuffer after readback, without
     * invalidating cached bindings. Reading through another FBO must not
     * redirect this draw or leave the depth attachment disconnected. */
    for(unsigned i=0;i<3;i++){v[i].position[2]=25;v[i].diffuse[1]=1;v[i].diffuse[2]=0;}
    assert(ssx_gl_draw(&d,v,3));ssx_gl_sync();
    assert(color[0][4+2*stride+2]==0xffff0000 && color[1][4+2*stride+2]==0xff00ff00);
    assert(abs((int)(z[4+2*stride+2]>>8)-25)<=1 && (z[4+2*stride+2]&255)==0x5a);
    for(unsigned i=0;i<3;i++) {
        uint32_t *buffer=i<2?color[i]:z,guard=i<2?0x55aa33cc:0xffffff5a;
        for(unsigned k=0;k<4;k++){assert(buffer[k]==guard);assert(buffer[words+4+k]==guard);}
        for(unsigned y=0;y<height;y++)for(unsigned x=width;x<stride;x++)assert(buffer[4+y*stride+x]==guard);
    }
    /* A later single dirty color surface still uses direct readback and sees
     * new guest pixels after invalidation, even after PBO allocation above. */
    color[0][4+2*stride+2]=0xff00ff00;ssx_gl_invalidate();
    d.color=color[0]+4;d.depth=NULL;d.depth_enable=d.depth_write=0;
    d.alpha_enable=1;d.alpha_func=0x200;
    assert(ssx_gl_draw(&d,v,3));ssx_gl_sync();assert(color[0][4+2*stride+2]==0xff00ff00);
}
#include "window_clip_cases.h"
static void window_clipping(void) {
    uint32_t color[256],depth[256],regs[2048];
    SsxGlDraw d={0};d.color=color;d.depth=depth;d.pitch=d.depth_pitch=64;
    d.width=d.height=d.clip_w=d.clip_h=16;d.registers=regs;
    d.color_mask=0xffffffff;d.depth_enable=d.depth_write=1;d.depth_func=0x201;
    SsxGlVertex v[3]={0};
    for(unsigned i=0;i<3;i++) {
        v[i].position[0]=i==1?64:0;v[i].position[1]=i==2?64:0;
        v[i].position[3]=1;v[i].diffuse[3]=1;
    }
    for(unsigned c=0;c<12;c++)for(unsigned mode=0;mode<2;mode++) {
        for(unsigned i=0;i<256;i++){color[i]=0;depth[i]=0xffffff5a;}
        memset(regs,0,sizeof regs);regs[0x2c0/4]=regs[0x2e0/4]=0x0fff0000;
        for(unsigned i=0;i<3;i++){v[i].position[2]=100;v[i].diffuse[0]=1;v[i].diffuse[1]=0;}
        ssx_gl_invalidate();ssx_gl_zpass_enable(0);assert(ssx_gl_draw(&d,v,3));
        // Change only window uniforms on an already bound program/target.
        unsigned char expected[256];window_case(c,regs,expected);regs[0x2b4/4]=mode;
        for(unsigned i=0;i<3;i++){v[i].position[2]=50;v[i].diffuse[0]=0;v[i].diffuse[1]=1;}
        ssx_gl_zpass_enable(1);ssx_gl_zpass_clear();assert(ssx_gl_draw(&d,v,3));ssx_gl_sync();
        unsigned count=0;
        for(unsigned i=0;i<256;i++) {
            int keep=expected[i]!=mode;count+=keep;
            assert(color[i]==(keep?0xff00ff00:0xffff0000));
            assert(abs((int)(depth[i]>>8)-(keep?50:100))<=1 && (depth[i]&255)==0x5a);
        }
        assert(ssx_gl_zpass_result()==count);
    }
    ssx_gl_zpass_enable(0);puts("24 GL window cases: colour, depth and zpass passed");
}
int main(int argc,char **argv){
    (void)argv;
#ifdef _WIN32
    _putenv_s("SSX_GL_SPECIALIZE",argc>1?"1":"0");
    _putenv_s("SSX_GL","1");
#else
    setenv("SSX_GL_SPECIALIZE",argc>1?"1":"0",1);
    setenv("SSX_GL","1",1);
#endif
    if(!ssx_gl_enabled())return 77;
    uint32_t color[256]={0},other[256]={0},depth[256],regs[2048]={0};
    SsxGlDraw d={0};d.color=color;d.pitch=64;d.width=d.height=d.clip_w=d.clip_h=16;
    d.color_mask=0xffffffff;d.registers=regs;regs[0x2c0/4]=regs[0x2e0/4]=0x0fff0000;d.blend_src=1;d.blend_dst=0;
    SsxGlVertex v[3]={0};
    for(int i=0;i<3;i++){v[i].position[0]=i==1?14:1;v[i].position[1]=i==2?14:1;v[i].position[2]=100;v[i].position[3]=1;}
    for(unsigned test=0;test<(argc>1?8u:256u);test++){
        memset(regs,0,sizeof regs);regs[0x2c0/4]=regs[0x2e0/4]=0x0fff0000;regs[0x1e60/4]=(test%4+1)|0x11100;
        regs[0x1e70/4]=4|(4<<5)|(4<<10)|(4<<15);
        for(int i=0;i<8;i++){
            regs[0xa60/4+i]=random_word();regs[0xa80/4+i]=random_word();
            regs[0xac0/4+i]=random_word();regs[0x260/4+i]=random_word();
            regs[0x1e40/4+i]=(random_word()&0xfe000)|0xcdu|0xc00u;
            regs[0xaa0/4+i]=(random_word()&0x3c000)|0xdcu|0xd00u;
        }
        regs[0x288/4]=random_word();regs[0x28c/4]=random_word();
        regs[0x1e20/4]=random_word();regs[0x1e24/4]=random_word();
        for(int k=0;k<4;k++){
            v[0].diffuse[k]=((int)(random_word()%513)-256)/256.f;
            v[0].specular[k]=((int)(random_word()%513)-256)/256.f;
            for(int t=0;t<4;t++)v[0].tex[t][k]=((int)(random_word()%513)-256)/256.f;
        }
        for(int i=1;i<3;i++){memcpy(v[i].diffuse,v[0].diffuse,16);memcpy(v[i].specular,v[0].specular,16);memcpy(v[i].tex,v[0].tex,64);}
        uint32_t expected=rc_fragment(regs,v[0].diffuse,v[0].specular,v[0].tex);
        memset(color,0,sizeof color);ssx_gl_invalidate();assert(ssx_gl_draw(&d,v,3));ssx_gl_sync();
        uint32_t actual=color[2*16+2];
        if(channel_difference(expected,actual)>1){fprintf(stderr,"case %u: %08x != %08x\n",test,actual,expected);return 1;}
    }
    /* Perspective interpolation applies to both color outputs and alpha.
     * Pixel (2.5,2.5) has normalized weights80:3:6 for W=(1,4,2). */
    const float diffuse_colors[3][4]={{1,0,0,1},{0,1,0,.5f},{0,0,1,0}};
    const float specular_colors[3][4]={{0,0,1,.25f},{1,0,0,.75f},{0,1,0,1}};
    for(unsigned i=0;i<3;i++) {
        v[i].position[3]=i==0?1:i==1?4:2;
        memcpy(v[i].diffuse,diffuse_colors[i],16);
        memcpy(v[i].specular,specular_colors[i],16);
    }
    memset(regs,0,sizeof regs);regs[0x2c0/4]=regs[0x2e0/4]=0x0fff0000;regs[0x1e60/4]=1;
    regs[0x288/4]=4;regs[0x28c/4]=0x1400;
    ssx_gl_invalidate();assert(ssx_gl_draw(&d,v,3));ssx_gl_sync();
    assert(channel_difference(color[34],0xeae50911)<=1);
    regs[0x288/4]=5;regs[0x28c/4]=0x1500;
    assert(ssx_gl_draw(&d,v,3));ssx_gl_sync();
    assert(channel_difference(color[34],0x510911e5)<=1);
    for(unsigned i=0;i<3;i++)v[i].position[3]=1;
    /* Shared depth survives switching color targets; rejected alpha writes no depth. */
    memset(regs,0,sizeof regs);regs[0x2c0/4]=regs[0x2e0/4]=0x0fff0000;for(int i=0;i<256;i++)depth[i]=0xffffff00;
    d.depth=depth;d.depth_pitch=64;d.depth_enable=d.depth_write=1;d.depth_func=0x201;
    for(int i=0;i<3;i++){v[i].diffuse[0]=v[i].diffuse[3]=1;v[i].diffuse[1]=v[i].diffuse[2]=0;}
    ssx_gl_invalidate();assert(ssx_gl_draw(&d,v,3));ssx_gl_sync();assert(color[34]==0xffff0000);
    d.color=other;for(int i=0;i<3;i++){v[i].position[2]=200;v[i].diffuse[0]=0;v[i].diffuse[2]=1;}
    assert(ssx_gl_draw(&d,v,3));ssx_gl_sync();assert(other[34]==0);
    for(int i=0;i<3;i++)v[i].position[2]=50;
    d.alpha_enable=1;d.alpha_func=0x200;assert(ssx_gl_draw(&d,v,3));ssx_gl_sync();assert(other[34]==0);
    d.alpha_enable=0;assert(ssx_gl_draw(&d,v,3));ssx_gl_sync();assert(other[34]==0xff0000ff);
    /* Visibility counts accumulate across reports, ignore rejected fragments,
     * and work even when color writes are masked (occlusion-only geometry). */
    d.depth_enable=d.depth_write=0;d.color_mask=0;
    ssx_gl_zpass_clear();ssx_gl_zpass_enable(1);
    assert(ssx_gl_draw(&d,v,3));
    uint32_t visible=ssx_gl_zpass_result();assert(visible>0 && visible<256);
    assert(ssx_gl_draw(&d,v,3));assert(ssx_gl_zpass_result()==2*visible);
    ssx_gl_zpass_clear();assert(ssx_gl_zpass_result()==0);
    d.alpha_enable=1;d.alpha_func=0x200;
    assert(ssx_gl_draw(&d,v,3));assert(ssx_gl_zpass_result()==0);
    d.alpha_enable=0;d.depth_enable=1;d.depth_func=0x200;
    assert(ssx_gl_draw(&d,v,3));assert(ssx_gl_zpass_result()==0);
    d.depth_enable=0;ssx_gl_zpass_enable(0);
    assert(ssx_gl_draw(&d,v,3));assert(ssx_gl_zpass_result()==0);
    ssx_gl_zpass_enable(1);assert(ssx_gl_draw(&d,v,3));
    ssx_gl_zpass_clear();assert(ssx_gl_zpass_result()==0);
    assert(ssx_gl_draw(&d,v,3));ssx_gl_zpass_enable(0);
    assert(ssx_gl_zpass_result()==visible);d.color_mask=0xffffffff;
    uint32_t texels[4]={0xff123456,0xffabcdef,0xff654321,0xfffedcba};
    memset(regs,0,sizeof regs);regs[0x2c0/4]=regs[0x2e0/4]=0x0fff0000;regs[0x1e60/4]=1;regs[0x1e70/4]=1;
    regs[0xac0/4]=0x08200000;regs[0x260/4]=0x18300000;
    regs[0x1e40/4]=regs[0xaa0/4]=0xc0;regs[0x288/4]=0x0c;regs[0x28c/4]=0x1c00;
    d.depth_enable=d.depth_write=0;
    d.textures[0]=(SsxGlTexture){texels,sizeof texels,2,2,0x12,8,1,1,1,1,texels,sample};
    for(int i=0;i<3;i++){v[i].tex[0][0]=.25;v[i].tex[0][1]=.25;v[i].tex[0][2]=0;v[i].tex[0][3]=1;}
    assert(ssx_gl_draw(&d,v,3));ssx_gl_sync();assert(other[34]==texels[0]);
    texels[0]=0xff987654;
    assert(ssx_gl_draw(&d,v,3));ssx_gl_sync();assert(other[34]==texels[0]);
    for(int i=0;i<3;i++)v[i].tex[0][0]=1.25;
    assert(ssx_gl_draw(&d,v,3));ssx_gl_sync();assert(other[34]==texels[0]);
    d.textures[0].wrap_u=2;
    assert(ssx_gl_draw(&d,v,3));ssx_gl_sync();assert(other[34]==texels[1]);
    d.textures[0].wrap_u=3;
    assert(ssx_gl_draw(&d,v,3));ssx_gl_sync();assert(other[34]==texels[1]);
    d.textures[0].min_filter=d.textures[0].mag_filter=2;
    for(int i=0;i<3;i++){v[i].tex[0][0]=.5;v[i].tex[0][1]=.5;}
    assert(ssx_gl_draw(&d,v,3));ssx_gl_sync();
    float average[4]={0};for(int i=0;i<4;i++){float c[4];rc_unpack(texels[i],c);for(int k=0;k<4;k++)average[k]+=c[k]*.25f;}
    assert(channel_difference(other[34],rc_pack(average))<=1);
    /* Distinct guest-authored mips must be used, not host-generated images.
     * Mutating only a small level must invalidate the cached whole chain. */
    uint32_t mip[21];
    for(unsigned i=0;i<21;i++)mip[i]=i<16?0xffff0000:i<20?0xff00ff00:0xff0000ff;
    SsxGlTexture saved=d.textures[0];
    d.textures[0]=(SsxGlTexture){.source=mip,.source_size=sizeof mip,.width=4,.height=4,
        .format=6,.wrap_u=1,.wrap_v=1,.normalized=1,.valid=1,.opaque=mip,
        .min_filter=3,.mag_filter=1,.levels=3,.sample_level=sample_mip,.min_lod=1,.max_lod=1};
    for(int i=0;i<3;i++){v[i].tex[0][0]=i==1?26:0;v[i].tex[0][1]=i==2?26:0;}
    assert(ssx_gl_draw(&d,v,3));ssx_gl_sync();assert(other[34]==0xff00ff00);
    d.textures[0].min_lod=d.textures[0].max_lod=2;
    assert(ssx_gl_draw(&d,v,3));ssx_gl_sync();assert(other[34]==0xff0000ff);
    mip[20]=0xffffff00;
    assert(ssx_gl_draw(&d,v,3));ssx_gl_sync();assert(other[34]==mip[20]);
    d.textures[0].min_filter=6;d.textures[0].min_lod=d.textures[0].max_lod=1.5;
    assert(ssx_gl_draw(&d,v,3));ssx_gl_sync();assert(channel_difference(other[34],0xff80ff00)<=1);
    d.textures[0].levels=1;d.textures[0].source_size=16*4;d.textures[0].min_lod=0;
    assert(ssx_gl_draw(&d,v,3));ssx_gl_sync();assert(other[34]==0xffff0000);
    d.textures[0].levels=3;d.textures[0].source_size=sizeof mip;
    d.textures[0].min_filter=3;d.textures[0].max_lod=2;
    assert(ssx_gl_draw(&d,v,3));ssx_gl_sync();assert(other[34]==mip[20]);
    d.textures[0]=saved;
    /* Exceed the source-copy budget, then revisit evicted and resident
     * sources. Each source reserves space for a larger guest texture chain. */
    size_t source_stride=1024u*1024u;
    unsigned char *sources=calloc(66,source_stride);assert(sources);
    d.textures[0].min_filter=d.textures[0].mag_filter=1;
    for(int i=0;i<3;i++){v[i].tex[0][0]=.75;v[i].tex[0][1]=.75;}
    for(unsigned i=0;i<66;i++) {
        uint32_t *pixels=(uint32_t *)(sources+i*source_stride);
        pixels[3]=0xff000000u|((i+1)*0x030201u);
        d.textures[0].source=d.textures[0].opaque=pixels;
        d.textures[0].source_size=source_stride;
        assert(ssx_gl_draw(&d,v,3));ssx_gl_sync();assert(other[34]==pixels[3]);
    }
    for(unsigned i=0;i<66;i+=13) {
        uint32_t *pixels=(uint32_t *)(sources+i*source_stride);
        pixels[3]^=0x00ffffffu;
        d.textures[0].source=d.textures[0].opaque=pixels;
        assert(ssx_gl_draw(&d,v,3));ssx_gl_sync();assert(other[34]==pixels[3]);
    }
    free(sources);
    /* Eviction/reallocation must invalidate cached framebuffer attachments,
     * including when the driver reuses a deleted texture name. */
    uint32_t targets[34][256]={{0}};
    memset(regs,0,sizeof regs);regs[0x2c0/4]=regs[0x2e0/4]=0x0fff0000;memset(d.textures,0,sizeof d.textures);
    d.depth=NULL;d.depth_enable=d.depth_write=0;d.alpha_enable=d.blend_enable=0;
    d.color_mask=0xffffffff;d.width=d.height=d.clip_w=d.clip_h=16;
    d.clip_x=d.clip_y=0;d.pitch=64;
    for(int i=0;i<3;i++) {
        memset(v[i].diffuse,0,sizeof v[i].diffuse);
        v[i].diffuse[1]=v[i].diffuse[3]=1;
        v[i].position[0]=i==1?14:1;v[i].position[1]=i==2?14:1;
        v[i].position[2]=100;v[i].position[3]=1;
    }
    for(unsigned i=0;i<34;i++) {
        d.color=targets[i];assert(ssx_gl_draw(&d,v,3));
        ssx_gl_sync();assert(targets[i][34]==0xff00ff00);
    }
    d.color=targets[0];d.width=d.clip_w=8;
    for(int i=0;i<3;i++){v[i].diffuse[1]=0;v[i].diffuse[2]=1;}
    assert(ssx_gl_draw(&d,v,3));ssx_gl_sync();assert(targets[0][34]==0xff0000ff);
    /* Repeated draws may keep the same linked shader while changing its
     * constants or alpha threshold. No invalidation masks a stale uniform. */
    d.width=d.clip_w=16;d.alpha_enable=0;
    for(unsigned bank=0;bank<3;bank++) {
        memset(regs,0,sizeof regs);regs[0x2c0/4]=regs[0x2e0/4]=0x0fff0000;regs[0x1e60/4]=1;
        if(bank<2) {
            regs[0xac0/4]=((bank+1)<<24)|0x00200000;
            regs[0x260/4]=((bank+0x11)<<24)|0x00300000;
            regs[0x1e40/4]=regs[0xaa0/4]=0xc0;
            regs[0x288/4]=0xc;regs[0x28c/4]=0x1c00;
        } else {regs[0x288/4]=1;regs[0x28c/4]=0x1100;}
        unsigned constant=bank==0?0xa60/4:bank==1?0xa80/4:0x1e20/4;
        for(unsigned change=0;change<4;change++) {
            regs[constant]=0xff123456u^(change*0x00312541u);
            uint32_t expected=rc_fragment(regs,v[0].diffuse,v[0].specular,v[0].tex);
            assert(channel_difference(expected,regs[constant])<=1);
            for(unsigned repeat=0;repeat<2;repeat++) {
                assert(ssx_gl_draw(&d,v,3));ssx_gl_sync();
                assert(channel_difference(targets[0][34],expected)<=1);
            }
        }
    }
    uint32_t before_alpha=targets[0][34];
    d.alpha_enable=1;d.alpha_func=0x204;d.alpha_ref=128;
    regs[0x1e20/4]=0x40123456;
    assert(ssx_gl_draw(&d,v,3));ssx_gl_sync();assert(targets[0][34]==before_alpha);
    d.alpha_ref=0;
    assert(ssx_gl_draw(&d,v,3));ssx_gl_sync();assert(channel_difference(targets[0][34],0x40123456)<=1);
    readback_pitches();window_clipping();
    printf("%u GL combiner cases, shared depth/alpha, texture update/wrap, bilinear, uniform-change and pitched readback checks passed\n",argc>1?8u:256u);return 0;
}
