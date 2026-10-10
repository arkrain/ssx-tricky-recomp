/* Opt-in, bounded GPU geometry. Failures leave the CPU renderer available. */
#include "nv2a_gpu_geometry.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
uint64_t ssx_gpu_geometry_batches,ssx_gpu_geometry_fallbacks;
#if defined(SSX_HAVE_EGL) && defined(SSX_GPU_GENERATED)
#include <epoxy/gl.h>
#include "nv2a_gpu_programs.inc"
static int enabled=-1,disabled,initialized;
static unsigned minimum_indices=512;
static GLuint programs[sizeof gpu_catalog/sizeof gpu_catalog[0]],geometry_shader;
static unsigned char failed[sizeof gpu_catalog/sizeof gpu_catalog[0]];
static GLuint vao,input_buffer,index_buffer,output_buffer,feedback_object,fbo,renderbuffer;
static GLuint compile(GLenum kind,const char *source){
    GLuint s=glCreateShader(kind);if(!s)return 0;glShaderSource(s,1,&source,NULL);glCompileShader(s);
    GLint ok;glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
    if(!ok){char log[4096];glGetShaderInfoLog(s,sizeof log,NULL,log);fprintf(stderr,"[GPU-GEOMETRY] compile failed; CPU fallback: %s\n",log);glDeleteShader(s);return 0;}return s;
}
static int initialize(void){
    if(initialized)return !disabled;initialized=1;
    if(!epoxy_has_gl_extension("GL_ARB_gpu_shader5") || !epoxy_has_gl_extension("GL_ARB_gpu_shader_fp64") || !epoxy_has_gl_extension("GL_ARB_transform_feedback2")){
        fprintf(stderr,"[GPU-GEOMETRY] required extensions unavailable; CPU fallback\n");disabled=1;return 0;}
    const char *renderer=(const char*)glGetString(GL_RENDERER);
    if(renderer && (strstr(renderer,"llvmpipe")||strstr(renderer,"softpipe"))){disabled=1;fprintf(stderr,"[GPU-GEOMETRY] software GL; CPU fallback\n");return 0;}
    geometry_shader=compile(GL_GEOMETRY_SHADER,gpu_clip_source);if(!geometry_shader){disabled=1;return 0;}
    GLint old_vao,old_array,old_fbo,old_renderbuffer;glGetIntegerv(GL_RENDERBUFFER_BINDING,&old_renderbuffer);glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&old_vao);glGetIntegerv(GL_ARRAY_BUFFER_BINDING,&old_array);glGetIntegerv(GL_FRAMEBUFFER_BINDING,&old_fbo);
    glGenVertexArrays(1,&vao);glBindVertexArray(vao);glGenBuffers(1,&input_buffer);glBindBuffer(GL_ARRAY_BUFFER,input_buffer);
    for(unsigned i=0;i<16;i++){glEnableVertexAttribArray(i);glVertexAttribPointer(i,4,GL_FLOAT,GL_FALSE,256,(void*)(uintptr_t)(i*16));}
    glGenBuffers(1,&index_buffer);glGenBuffers(1,&output_buffer);glGenTransformFeedbacks(1,&feedback_object);
    glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);glGenRenderbuffers(1,&renderbuffer);glBindRenderbuffer(GL_RENDERBUFFER,renderbuffer);
    glRenderbufferStorage(GL_RENDERBUFFER,GL_RGBA8,1,1);glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,renderbuffer);
    int complete=glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE;
    glBindRenderbuffer(GL_RENDERBUFFER,old_renderbuffer);glBindFramebuffer(GL_FRAMEBUFFER,old_fbo);glBindVertexArray(old_vao);glBindBuffer(GL_ARRAY_BUFFER,old_array);
    if(!complete || glGetError()!=GL_NO_ERROR){disabled=1;fprintf(stderr,"[GPU-GEOMETRY] resource initialization failed; CPU fallback\n");return 0;}
    fprintf(stderr,"[GPU-GEOMETRY] experimental indexed two-pass path; minimum %u indices\n",minimum_indices);return 1;
}
int ssx_gpu_geometry_prepare(const uint32_t (*code)[4],unsigned available,const float *context,unsigned indices){
    if(enabled<0){const char *e=getenv("SSX_GL_GPU_GEOMETRY");enabled=e && !strcmp(e,"1");
        const char *m=getenv("SSX_GL_GPU_GEOMETRY_MIN_INDICES");if(m){char *end;unsigned long n=strtoul(m,&end,10);if(*m && !*end && n>=3 && n<=65536)minimum_indices=(unsigned)n;}}
    if(!enabled || disabled || indices<minimum_indices || !code || !context)return 0;
    unsigned number=0;for(;number<sizeof gpu_catalog/sizeof gpu_catalog[0];number++){
        if(gpu_catalog[number].eligible && available>=gpu_catalog[number].count && !memcmp(code,gpu_catalog[number].words,gpu_catalog[number].count*16))break;}
    if(number==sizeof gpu_catalog/sizeof gpu_catalog[0] || failed[number])return 0;
    for(unsigned k=0;k<768;k++)if(!isfinite(context[k]))return 0;
    if(!initialize())return 0;
    if(!programs[number]){
        GLuint vs=compile(GL_VERTEX_SHADER,gpu_catalog[number].source);if(!vs){failed[number]=1;return 0;}
        GLuint p=glCreateProgram();glAttachShader(p,vs);glAttachShader(p,geometry_shader);const char *varying="captured";
        glTransformFeedbackVaryings(p,1,&varying,GL_INTERLEAVED_ATTRIBS);glLinkProgram(p);glDeleteShader(vs);
        GLint ok;glGetProgramiv(p,GL_LINK_STATUS,&ok);
        if(!ok){char log[4096];glGetProgramInfoLog(p,sizeof log,NULL,log);fprintf(stderr,"[GPU-GEOMETRY] program%u link failed; CPU fallback: %s\n",number,log);glDeleteProgram(p);failed[number]=1;return 0;}
        programs[number]=p;fprintf(stderr,"[GPU-GEOMETRY] compiled program%u\n",number);
    }
    return (int)number+1;
}
int ssx_gpu_geometry_run(int token,const float *context,const float *inputs,unsigned vertices,
                        const uint32_t *indices,unsigned count,const SsxGlDraw *d,unsigned *buffer,unsigned *feedback){
    if(disabled || token<1 || (unsigned)token>sizeof gpu_catalog/sizeof gpu_catalog[0] || !vertices || !count || count%3 || vertices>65536 || count>65536)return 0;
    GLuint p=programs[token-1];if(!p)return 0;
    GLint old_fbo;glGetIntegerv(GL_FRAMEBUFFER_BINDING,&old_fbo);GLboolean discard=glIsEnabled(GL_RASTERIZER_DISCARD);
    glBindFramebuffer(GL_FRAMEBUFFER,fbo);glUseProgram(p);glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER,input_buffer);glBufferData(GL_ARRAY_BUFFER,(size_t)vertices*256,inputs,GL_STREAM_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,index_buffer);glBufferData(GL_ELEMENT_ARRAY_BUFFER,(size_t)count*4,indices,GL_STREAM_DRAW);
    glUniform4fv(glGetUniformLocation(p,"context_bank"),192,context);glUniform4f(glGetUniformLocation(p,"clip_bounds"),d->clip_x,d->clip_y,d->clip_w,d->clip_h);
    float far;memcpy(&far,&d->registers[0x398/4],4);if(!isfinite(far)||far<=0)far=16777215.f;
    glUniform1f(glGetUniformLocation(p,"clip_far"),far);glUniform1i(glGetUniformLocation(p,"cull_enable"),d->registers[0x308/4]!=0);
    glUniform1i(glGetUniformLocation(p,"front_clockwise"),d->registers[0x3a0/4]==0x900);glUniform1i(glGetUniformLocation(p,"cull_face"),d->registers[0x39c/4]);
    glBindTransformFeedback(GL_TRANSFORM_FEEDBACK,feedback_object);glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER,output_buffer);
    glBufferData(GL_TRANSFORM_FEEDBACK_BUFFER,(size_t)count*8*112,NULL,GL_STREAM_DRAW);glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER,0,output_buffer);
    GLenum error=glGetError();
    if(error==GL_NO_ERROR){glEnable(GL_RASTERIZER_DISCARD);glBeginTransformFeedback(GL_TRIANGLES);glDrawElements(GL_TRIANGLES,count,GL_UNSIGNED_INT,0);glEndTransformFeedback();error=glGetError();}
    if(!discard)glDisable(GL_RASTERIZER_DISCARD);glBindFramebuffer(GL_FRAMEBUFFER,old_fbo);
    if(error!=GL_NO_ERROR){disabled=1;ssx_gpu_geometry_fallbacks++;fprintf(stderr,"[GPU-GEOMETRY] error %x; disabling experiment, CPU fallback\n",error);return 0;}
    *buffer=output_buffer;*feedback=feedback_object;ssx_gpu_geometry_batches++;return 1;
}
#else
int ssx_gpu_geometry_prepare(const uint32_t (*c)[4],unsigned a,const float *v,unsigned n){(void)c;(void)a;(void)v;(void)n;return 0;}
int ssx_gpu_geometry_run(int t,const float*c,const float*i,unsigned v,const uint32_t*x,unsigned n,const SsxGlDraw*d,unsigned*b,unsigned*f){(void)t;(void)c;(void)i;(void)v;(void)x;(void)n;(void)d;(void)b;(void)f;return 0;}
#endif
