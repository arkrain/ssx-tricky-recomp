/* Enhanced rendering must retain subpixel detail without overrunning native RAM.
 * Also verify CPU cinematic clears preserve the untouched enhanced image. */
#include "nv2a_gl.h"
#include <epoxy/gl.h>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
int main(int argc,char **argv) {
    const unsigned scale=argc>1?(unsigned)atoi(argv[1]):2;assert(scale>=2 && scale<=4);
    const char *scale_text=argc>1?argv[1]:"2";
#ifdef _WIN32
    _putenv_s("SSX_GL","1");_putenv_s("SSX_GL_RENDER_SCALE",scale_text);_putenv_s("SSX_GL_ANISOTROPY","16");
#else
    setenv("SSX_GL","1",1);setenv("SSX_GL_RENDER_SCALE",scale_text,1);
    setenv("SSX_GL_ANISOTROPY","16",1);
#endif
    if(!ssx_gl_enabled())return 77;
    enum { W=8,H=8,P=12,N=P*H };
    uint32_t color[N+8],depth[N+8],regs[2048]={0};
    for(unsigned i=0;i<N+8;i++){color[i]=0x12345678;depth[i]=0xabcdef98;}
    for(unsigned y=0;y<H;y++)for(unsigned x=0;x<W;x++){color[4+y*P+x]=0xff000000;depth[4+y*P+x]=0xffffff5a;}
    regs[0x2c0/4]=regs[0x2e0/4]=0x0fff0000;
    SsxGlDraw d={0};d.color=color+4;d.depth=depth+4;d.pitch=d.depth_pitch=P*4;
    d.width=d.height=d.clip_w=d.clip_h=8;d.color_mask=0xffffffff;d.depth_enable=d.depth_write=1;d.depth_func=0x201;d.registers=regs;
    SsxGlVertex v[3]={0};
    for(unsigned i=0;i<3;i++){v[i].position[0]=i==1?8:0;v[i].position[1]=i==2?8:0;v[i].position[2]=50;v[i].position[3]=1;v[i].diffuse[0]=v[i].diffuse[3]=1;}
    assert(ssx_gl_draw(&d,v,3));assert(color[4]==0xff000000);ssx_gl_sync();
    if(epoxy_has_gl_extension("GL_EXT_texture_filter_anisotropic")) {
        GLfloat maximum;glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT,&maximum);
        for(unsigned i=0;i<4;i++){GLint sampler;GLfloat actual;glGetIntegeri_v(GL_SAMPLER_BINDING,i,&sampler);glGetSamplerParameterfv(sampler,GL_TEXTURE_MAX_ANISOTROPY_EXT,&actual);assert(actual==(maximum<16?maximum:16));}
    }
    unsigned w=0,h=0;const uint32_t *hd=ssx_gl_present(color+4,P*4,&w,&h);
    assert(hd && w==8*scale && h==8*scale);
    unsigned mixed=0;
    for(unsigned y=0;y<8;y++)for(unsigned x=0;x<8;x++) {
        unsigned red=0,black=0;
        for(unsigned j=0;j<scale;j++)for(unsigned i=0;i<scale;i++) {
            uint32_t q=hd[(y*scale+j)*8*scale+x*scale+i];assert(q==0xffff0000 || q==0xff000000);red+=q==0xffff0000;black+=q==0xff000000;
        }
        if(red && black){mixed++;unsigned r=(color[4+y*P+x]>>16)&255;if(scale%2==0)assert(r>0 && r<255);
            else assert(r==((hd[(y*scale+scale/2)*8*scale+x*scale+scale/2]>>16)&255));}
    }
    assert(mixed>0);uint32_t original[1024];memcpy(original,hd,8*scale*8*scale*4);
    // CPU clear of top two rows (normal cinematic bars), followed by its GPU mirror.
    ssx_gl_invalidate();for(unsigned y=0;y<2;y++)for(unsigned x=0;x<8;x++)color[4+y*P+x]=0xffffffff;
    ssx_gl_clear_surface(color+4,P*4,0,0,0,8,2,0xffffffff,0xffffffff);
    hd=ssx_gl_present(color+4,P*4,&w,&h);assert(hd);
    for(unsigned y=0;y<8*scale;y++)for(unsigned x=0;x<8*scale;x++)assert(hd[y*8*scale+x]==(y<2*scale?0xffffffff:original[y*8*scale+x]));
    // A partial channel clear may not overwrite retained red coverage.
    ssx_gl_invalidate();for(unsigned y=2;y<6;y++)for(unsigned x=2;x<6;x++)color[4+y*P+x]|=255;
    ssx_gl_clear_surface(color+4,P*4,0,2,2,6,6,255,255);
    hd=ssx_gl_present(color+4,P*4,&w,&h);assert(hd);
    for(unsigned y=2*scale;y<6*scale;y++)for(unsigned x=2*scale;x<6*scale;x++)assert(hd[y*8*scale+x]==(original[y*8*scale+x]|255));
    for(unsigned i=0;i<4;i++){assert(color[i]==0x12345678 && color[N+4+i]==0x12345678);assert(depth[i]==0xabcdef98 && depth[N+4+i]==0xabcdef98);}
    for(unsigned y=0;y<H;y++)for(unsigned x=W;x<P;x++){assert(color[4+y*P+x]==0x12345678);assert(depth[4+y*P+x]==0xabcdef98);}
    // Invalidated CPU writes without an enhanced mirror must not publish stale HD.
    ssx_gl_invalidate();assert(!ssx_gl_present(color+4,P*4,&w,&h));
    // Inclusive guest window clipping must cover exactly one scaled host cell.
    for(unsigned i=0;i<8;i++){regs[0x2c0/4+i]=regs[0x2e0/4+i]=1;}
    regs[0x2c0/4]=3|(3<<16);regs[0x2e0/4]=4|(4<<16);
    for(unsigned y=0;y<8;y++)for(unsigned x=0;x<8;x++)color[4+y*P+x]=0xff000000;
    for(unsigned i=0;i<3;i++){v[i].position[0]=i==1?32:0;v[i].position[1]=i==2?32:0;v[i].diffuse[0]=0;v[i].diffuse[1]=1;}
    d.depth_enable=d.depth_write=0;ssx_gl_zpass_clear();ssx_gl_zpass_enable(1);
    assert(ssx_gl_draw(&d,v,3));assert(ssx_gl_zpass_result()==1);ssx_gl_zpass_enable(0);ssx_gl_sync();
    hd=ssx_gl_present(color+4,P*4,&w,&h);assert(hd);
    for(unsigned y=0;y<8*scale;y++)for(unsigned x=0;x<8*scale;x++)assert(hd[y*8*scale+x]==(x>=3*scale && x<4*scale && y>=4*scale && y<5*scale?0xff00ff00:0xff000000));
    // A differently sized target sharing the same guest address must never
    // resurrect the old HD image when a later native clear is mirrored.
    ssx_gl_invalidate();d.width=d.height=d.clip_w=d.clip_h=4;
    regs[0x2c0/4]=regs[0x2e0/4]=0x0fff0000;
    assert(ssx_gl_draw(&d,v,3));ssx_gl_sync();ssx_gl_invalidate();
    for(unsigned y=0;y<4;y++)for(unsigned x=0;x<4;x++)color[4+y*P+x]=0xffffffff;
    ssx_gl_clear_surface(color+4,P*4,0,0,0,4,4,0xffffffff,0xffffffff);
    hd=ssx_gl_present(color+4,P*4,&w,&h);assert(hd && w==4*scale && h==4*scale);
    for(unsigned i=0;i<16*scale*scale;i++)assert(hd[i]==0xffffffff);
    puts("HD subpixel coverage, native resolve/padding, partial clears and stale-frame rejection passed");return 0;
}
