/* Optional OpenGL rasterizer. Guest shaders still execute in the checked NV2A
 * interpreter; host GL handles coverage, texture sampling, combiners and depth.
 * All GL calls belong to the pushbuffer worker thread. */
#include "nv2a_gl.h"
#include "nv2a_gpu_geometry.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if defined(SSX_HAVE_EGL) || defined(SSX_HAVE_WGL)
#ifdef SSX_HAVE_WGL
#include <epoxy/wgl.h>
#else
#include <epoxy/egl.h>
#endif
#include <epoxy/gl.h>
#include <stdatomic.h>
#include "nv2a_gl_shader.h"

#ifdef SSX_HAVE_WGL
static HWND context_window;
static HDC context_dc;
static HGLRC context;
#else
static EGLDisplay display;
static EGLContext context;
#endif
static GLuint program,vao,vbo,fbo,samplers[4];
static GLuint geometry_buffer,geometry_feedback,bound_vertex_buffer;
static atomic_int initialized;
static unsigned serial;
static unsigned render_scale=1;
static float anisotropy=1;
static unsigned draw_scale=1;
static GLuint attached_color,attached_depth;
static int framebuffer_valid;
static int uniform_state_valid;
static int state_cache_enabled(void) {
    static int enabled=-1;
    if(enabled<0){const char *v=getenv("SSX_GL_STATE_CACHE");enabled=!(v && !strcmp(v,"0"));}
    return enabled;
}

typedef struct {
    void *address;
    unsigned pitch,width,height,depth,loaded,dirty,used;
    GLuint texture,native_texture;
    unsigned scale,preserve;
} Image;
static Image images[32];
typedef struct {
    const void *address;
    size_t bytes;
    unsigned char *source_copy;
    unsigned width,height,format,pitch,levels,used;
    GLuint texture;
} Texture;
static Texture textures[256];
typedef struct {
    GLint extent,texMode,texValid,texNormalized,texSize,control,combiner;
    GLint constant0,constant1,final0,final1,finalInput;
    GLint alphaEnable,alphaFunc,alphaRef,legacyTextured,windowExclusive,windowRect;
} Uniforms;
static Uniforms uniforms;
static const char vertex_source[]=
    "#version 330 core\n"
    "layout(location=0)in vec4 position;layout(location=1)in vec4 inDiffuse;"
    "layout(location=2)in vec4 inSpecular;layout(location=3)in vec4 inTex[4];"
    "uniform vec2 extent;out vec4 diffuse;out vec4 specular;"
    "out vec4 texcoord[4];void main(){float w=position.w;"
    "gl_Position=vec4((position.xy/extent*2.0-1.0)*w,(position.z/16777215.0*2.0-1.0)*w,w);"
    "diffuse=inDiffuse;specular=inSpecular;for(int i=0;i<4;i++)texcoord[i]=inTex[i];}";
static GLuint shader(GLenum type,const char *source) {
    GLuint s=glCreateShader(type);GLint ok;
    glShaderSource(s,1,&source,NULL);glCompileShader(s);glGetShaderiv(s,GL_COMPILE_STATUS,&ok);
    if(!ok){char log[4096];glGetShaderInfoLog(s,sizeof log,NULL,log);fprintf(stderr,"[GL] shader: %s\n",log);glDeleteShader(s);return 0;}
    return s;
}
static Uniforms program_uniforms(GLuint object) {
    Uniforms u;GLuint program=object;glUseProgram(program);
#define U(name) u.name=glGetUniformLocation(program,#name)
    U(extent);U(texMode);U(texValid);U(texNormalized);U(texSize);U(control);U(combiner);
    U(constant0);U(constant1);U(final0);U(final1);U(finalInput);
    U(alphaEnable);U(alphaFunc);U(alphaRef);U(legacyTextured);U(windowExclusive);U(windowRect);
#undef U
    const char *sampler_names[]={"tex0","tex1","tex2","tex3"};
    for(int i=0;i<4;i++)glUniform1i(glGetUniformLocation(program,sampler_names[i]),i);
    return u;
}
#ifdef SSX_HAVE_WGL
/* A private window supplies the pixel format; the shared renderer draws into
 * FBOs and publishes guest surfaces, never swaps this hidden window. Context
 * creation and all subsequent GL operations run on the pushbuffer thread. */
static int open_context(void) {
    HINSTANCE instance=GetModuleHandleA(NULL);
    WNDCLASSA cls={0};cls.style=CS_OWNDC;cls.lpfnWndProc=DefWindowProcA;
    cls.hInstance=instance;cls.lpszClassName="SSXPrivateOpenGL";
    if(!RegisterClassA(&cls) && GetLastError()!=ERROR_CLASS_ALREADY_EXISTS)return 0;
    context_window=CreateWindowA(cls.lpszClassName,"SSX OpenGL context",WS_POPUP,
        0,0,1,1,NULL,NULL,instance,NULL);
    if(!context_window)return 0;
    context_dc=GetDC(context_window);
    PIXELFORMATDESCRIPTOR format={0};format.nSize=sizeof format;
    format.nVersion=1;format.dwFlags=PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL;
    format.iPixelType=PFD_TYPE_RGBA;format.cColorBits=32;
    format.cDepthBits=24;format.cStencilBits=8;format.iLayerType=PFD_MAIN_PLANE;
    int pixel=context_dc?ChoosePixelFormat(context_dc,&format):0;
    HGLRC bootstrap=NULL;
    if(!pixel || !SetPixelFormat(context_dc,pixel,&format))goto fail;
    bootstrap=wglCreateContext(context_dc);
    if(!bootstrap || !wglMakeCurrent(context_dc,bootstrap))goto fail;
    if(!epoxy_has_wgl_extension(context_dc,"WGL_ARB_create_context") ||
       !epoxy_has_wgl_extension(context_dc,"WGL_ARB_create_context_profile"))goto fail;
    const int attrs[]={WGL_CONTEXT_MAJOR_VERSION_ARB,3,WGL_CONTEXT_MINOR_VERSION_ARB,3,
        WGL_CONTEXT_PROFILE_MASK_ARB,WGL_CONTEXT_CORE_PROFILE_BIT_ARB,0};
    context=wglCreateContextAttribsARB(context_dc,NULL,attrs);
    if(!context || !wglMakeCurrent(context_dc,context))goto fail;
    wglDeleteContext(bootstrap);return 1;
fail:
    wglMakeCurrent(NULL,NULL);
    if(context){wglDeleteContext(context);context=NULL;}
    if(bootstrap)wglDeleteContext(bootstrap);
    if(context_dc){ReleaseDC(context_window,context_dc);context_dc=NULL;}
    DestroyWindow(context_window);context_window=NULL;return 0;
}
static int context_current(void){return wglGetCurrentContext()==context;}
#else
static int open_display(EGLDisplay candidate) {
    EGLint major,minor,n;
    if(candidate==EGL_NO_DISPLAY || !eglInitialize(candidate,&major,&minor))return 0;
    if(!eglBindAPI(EGL_OPENGL_API)){eglTerminate(candidate);return 0;}
    EGLint attrs[]={EGL_SURFACE_TYPE,EGL_PBUFFER_BIT,EGL_RENDERABLE_TYPE,EGL_OPENGL_BIT,EGL_RED_SIZE,8,EGL_GREEN_SIZE,8,EGL_BLUE_SIZE,8,EGL_NONE};
    EGLConfig config;
    if(!eglChooseConfig(candidate,attrs,&config,1,&n)||!n){eglTerminate(candidate);return 0;}
    EGLint ca[]={EGL_CONTEXT_MAJOR_VERSION,3,EGL_CONTEXT_MINOR_VERSION,3,EGL_CONTEXT_OPENGL_PROFILE_MASK,EGL_CONTEXT_OPENGL_CORE_PROFILE_BIT,EGL_NONE};
    EGLContext ctx=eglCreateContext(candidate,config,EGL_NO_CONTEXT,ca);
    if(ctx==EGL_NO_CONTEXT || !eglMakeCurrent(candidate,EGL_NO_SURFACE,EGL_NO_SURFACE,ctx)){
        if(ctx!=EGL_NO_CONTEXT)eglDestroyContext(candidate,ctx);eglTerminate(candidate);return 0;
    }
    display=candidate;context=ctx;return 1;
}
static int context_current(void){return eglGetCurrentContext()==context;}
#endif
int ssx_gl_enabled(void) {
    if(initialized)return initialized>0;
    const char *option=getenv("SSX_GL");
    if(!option || strcmp(option,"1")){initialized=-1;return 0;}
    initialized=-1;
#ifdef SSX_HAVE_WGL
    if(!open_context()){fprintf(stderr,"[GL] WGL 3.3 context unavailable\n");return 0;}
#else
    /* Prefer a hardware device independently of the presentation/display
     * driver. Surfaceless Mesa remains usable for headless regression tests. */
    PFNEGLQUERYDEVICESEXTPROC query=(PFNEGLQUERYDEVICESEXTPROC)eglGetProcAddress("eglQueryDevicesEXT");
    PFNEGLQUERYDEVICESTRINGEXTPROC description=(PFNEGLQUERYDEVICESTRINGEXTPROC)eglGetProcAddress("eglQueryDeviceStringEXT");
    EGLDeviceEXT devices[16];EGLint device_count=0;
    if(query && description && query(16,devices,&device_count))for(int i=0;i<device_count;i++) {
        const char *extensions=description(devices[i],EGL_EXTENSIONS);
        if(extensions && strstr(extensions,"EGL_MESA_device_software"))continue;
        if(open_display(eglGetPlatformDisplayEXT(EGL_PLATFORM_DEVICE_EXT,devices[i],NULL)))break;
    }
    if(context==EGL_NO_CONTEXT && !open_display(eglGetPlatformDisplayEXT(EGL_PLATFORM_SURFACELESS_MESA,EGL_DEFAULT_DISPLAY,NULL))){fprintf(stderr,"[GL] EGL context unavailable\n");return 0;}
#endif
    GLuint vs=shader(GL_VERTEX_SHADER,vertex_source),fs=shader(GL_FRAGMENT_SHADER,ssx_gl_fragment_source);
    if(!vs||!fs)return 0;
    program=glCreateProgram();glAttachShader(program,vs);glAttachShader(program,fs);glLinkProgram(program);
    glDeleteShader(vs);glDeleteShader(fs);GLint ok;glGetProgramiv(program,GL_LINK_STATUS,&ok);
    if(!ok){char log[4096];glGetProgramInfoLog(program,sizeof log,NULL,log);fprintf(stderr,"[GL] link: %s\n",log);return 0;}
    glUseProgram(program);
    uniforms=program_uniforms(program);
    glGenVertexArrays(1,&vao);glBindVertexArray(vao);glGenBuffers(1,&vbo);glBindBuffer(GL_ARRAY_BUFFER,vbo);
    for(int i=0;i<7;i++){glEnableVertexAttribArray(i);glVertexAttribPointer(i,4,GL_FLOAT,GL_FALSE,sizeof(SsxGlVertex),(void*)(uintptr_t)(i*16));}
    glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    glGenSamplers(4,samplers);
    glDisable(GL_DITHER);glDisable(GL_MULTISAMPLE);
    const char *scale_option=getenv("SSX_GL_RENDER_SCALE");
    if(scale_option && scale_option[0]>='1' && scale_option[0]<='4' && !scale_option[1])render_scale=(unsigned)(scale_option[0]-'0');
    const char *aniso_option=getenv("SSX_GL_ANISOTROPY");
    if(aniso_option && epoxy_has_gl_extension("GL_EXT_texture_filter_anisotropic")) {
        char *end;float requested=strtof(aniso_option,&end),maximum=1;
        glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT,&maximum);
        if(*aniso_option && !*end && requested>=1 && requested<=16)
            anisotropy=requested<maximum?requested:maximum;
    }
    if(anisotropy>1)for(unsigned i=0;i<4;i++)
        glSamplerParameterf(samplers[i],GL_TEXTURE_MAX_ANISOTROPY_EXT,anisotropy);
    initialized=1;
    fprintf(stderr,"[GL] enhancements: render scale %ux, anisotropy %.0fx\n",render_scale,anisotropy);
    fprintf(stderr,"[GL] rasterizer: %s / %s\n",glGetString(GL_RENDERER),glGetString(GL_VERSION));
    return 1;
}
static GLuint zpass_query;
static int zpass_enabled;
static unsigned zpass_scale=1;
static uint32_t zpass_total;
static void zpass_finish(int retain) {
    if(!zpass_query)return;
    glEndQuery(GL_SAMPLES_PASSED);
    if(retain) {
        GLuint result=0;glGetQueryObjectuiv(zpass_query,GL_QUERY_RESULT,&result);
        zpass_total+=result/(zpass_scale*zpass_scale);
    }
    glDeleteQueries(1,&zpass_query);zpass_query=0;
}
void ssx_gl_zpass_enable(int enabled) {
    if(!enabled)zpass_finish(1);
    zpass_enabled=enabled!=0;
}
void ssx_gl_zpass_clear(void) { zpass_finish(0);zpass_total=0; }
uint32_t ssx_gl_zpass_result(void) { zpass_finish(1);return zpass_total; }
/* Queue color/depth transfers together, then wait before publishing any
 * guest-visible surface. Tight PBO rows avoid copying undefined padding back
 * into guest memory. Failed allocation/mapping retains the direct path. */
/* A read framebuffer gives drivers a render-target readback path. Keep
 * texture retrieval available for unsupported/error cases and comparisons.
 * Restoring both bindings preserves the renderer's framebuffer-state cache. */
/* Export packed depth through integer color targets without changing guest-visible completion. */
static GLuint export_program(const char *fragment) {
    const char *vs="#version 330 core\nvoid main(){vec2 p=vec2((gl_VertexID<<1)&2,gl_VertexID&2);gl_Position=vec4(p*2.0-1.0,0,1);}";
    GLuint v=shader(GL_VERTEX_SHADER,vs),f=shader(GL_FRAGMENT_SHADER,fragment),p=0;
    if(v && f){p=glCreateProgram();glAttachShader(p,v);glAttachShader(p,f);glLinkProgram(p);GLint ok;glGetProgramiv(p,GL_LINK_STATUS,&ok);if(!ok){glDeleteProgram(p);p=0;}}
    if(v)glDeleteShader(v);if(f)glDeleteShader(f);return p;
}
static int export_precision(void);
static int export_depth(const Image *im,void *destination) {
    static int available=-1,failed;
    static GLuint programs[2],targets[2],framebuffer,array;
    static unsigned width,height;
    if(available<0){const char *v=getenv("SSX_GL_DEPTH_EXPORT");available=!(v && !strcmp(v,"0")) && epoxy_has_gl_extension("GL_ARB_stencil_texturing");}
    if(!available || failed || !im->width || !im->height || (size_t)im->width*im->height>8u*1024u*1024u)return 0;
    static int precision_state;
    if(!precision_state) {
        precision_state=1; /* Recursive calls exercise this same export path. */
        if(!export_precision()) {
            failed=1;precision_state=-1;
            fprintf(stderr,"[GL] depth export precision check failed; retaining direct readback\n");return 0;
        }
        precision_state=2;
        fprintf(stderr,"[GL] depth export verified all 16777216 depth codes\n");
    }
    /* Export triangles must not contribute to the guest's occlusion query. */
    zpass_finish(1);
    GLint old_program,old_array,old_draw,old_read,old_active,old_view[4],old_textures[2],old_samplers[2],old_mode,old_compare,old_unpack,old_polygon[2];
    GLboolean old_mask[4],enabled[9];
    const GLenum caps[]={GL_DEPTH_TEST,GL_STENCIL_TEST,GL_BLEND,GL_SCISSOR_TEST,GL_CULL_FACE,GL_DITHER,GL_MULTISAMPLE,GL_FRAMEBUFFER_SRGB,GL_RASTERIZER_DISCARD};
    glGetIntegerv(GL_CURRENT_PROGRAM,&old_program);glGetIntegerv(GL_VERTEX_ARRAY_BINDING,&old_array);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&old_draw);glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&old_read);
    glGetIntegerv(GL_ACTIVE_TEXTURE,&old_active);glGetIntegerv(GL_VIEWPORT,old_view);glGetBooleanv(GL_COLOR_WRITEMASK,old_mask);
    glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&old_unpack);glGetIntegerv(GL_POLYGON_MODE,old_polygon);
    for(unsigned i=0;i<9;i++){enabled[i]=glIsEnabled(caps[i]);glDisable(caps[i]);}
    for(unsigned i=0;i<2;i++){glActiveTexture(GL_TEXTURE0+i);glGetIntegerv(GL_TEXTURE_BINDING_2D,&old_textures[i]);glGetIntegeri_v(GL_SAMPLER_BINDING,i,&old_samplers[i]);glBindSampler(i,0);}
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,im->texture);
    glGetTexParameteriv(GL_TEXTURE_2D,GL_DEPTH_STENCIL_TEXTURE_MODE,&old_mode);glGetTexParameteriv(GL_TEXTURE_2D,GL_TEXTURE_COMPARE_MODE,&old_compare);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_COMPARE_MODE,GL_NONE);
    glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);glPolygonMode(GL_FRONT_AND_BACK,GL_FILL);
    int ok=0;
    if(!programs[0]) {
        programs[0]=export_program("#version 330 core\nuniform sampler2D source;layout(location=0) out uint result;void main(){float d=texelFetch(source,ivec2(gl_FragCoord.xy),0).r;result=uint(roundEven(d*16777215.0));}");
        programs[1]=export_program("#version 330 core\nuniform usampler2D source;uniform usampler2D depths;layout(location=0) out uint result;void main(){ivec2 p=ivec2(gl_FragCoord.xy);result=(texelFetch(depths,p,0).r<<8)|texelFetch(source,p,0).r;}");
        if(!programs[0] || !programs[1])goto restore;
        for(unsigned i=0;i<2;i++){glUseProgram(programs[i]);glUniform1i(glGetUniformLocation(programs[i],"source"),0);}
        glUniform1i(glGetUniformLocation(programs[1],"depths"),1);
        glGenTextures(2,targets);glGenFramebuffers(1,&framebuffer);glGenVertexArrays(1,&array);
    }
    if(width!=im->width || height!=im->height) {
        for(unsigned i=0;i<2;i++) {
            glBindTexture(GL_TEXTURE_2D,targets[i]);glTexImage2D(GL_TEXTURE_2D,0,GL_R32UI,im->width,im->height,0,GL_RED_INTEGER,GL_UNSIGNED_INT,NULL);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        }
        if(glGetError()!=GL_NO_ERROR)goto restore;
        width=im->width;height=im->height;
    }
    glBindFramebuffer(GL_FRAMEBUFFER,framebuffer);glBindVertexArray(array);glViewport(0,0,width,height);glColorMask(1,1,1,1);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,targets[0],0);glDrawBuffer(GL_COLOR_ATTACHMENT0);glReadBuffer(GL_COLOR_ATTACHMENT0);
    if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE)goto restore;
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,im->texture);glTexParameteri(GL_TEXTURE_2D,GL_DEPTH_STENCIL_TEXTURE_MODE,GL_DEPTH_COMPONENT);
    glUseProgram(programs[0]);glDrawArrays(GL_TRIANGLES,0,3);
    glTexParameteri(GL_TEXTURE_2D,GL_DEPTH_STENCIL_TEXTURE_MODE,GL_STENCIL_INDEX);
    glActiveTexture(GL_TEXTURE1);glBindTexture(GL_TEXTURE_2D,targets[0]);
    glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,targets[1],0);glUseProgram(programs[1]);glDrawArrays(GL_TRIANGLES,0,3);
    glReadPixels(0,0,width,height,GL_RED_INTEGER,GL_UNSIGNED_INT,destination);
    ok=glGetError()==GL_NO_ERROR;
restore:
    glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,im->texture);glTexParameteri(GL_TEXTURE_2D,GL_DEPTH_STENCIL_TEXTURE_MODE,old_mode);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_COMPARE_MODE,old_compare);
    for(unsigned i=0;i<2;i++){glActiveTexture(GL_TEXTURE0+i);glBindTexture(GL_TEXTURE_2D,old_textures[i]);glBindSampler(i,old_samplers[i]);}
    glActiveTexture(old_active);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,old_unpack);
    glUseProgram(old_program);glBindVertexArray(old_array);glBindFramebuffer(GL_DRAW_FRAMEBUFFER,old_draw);glBindFramebuffer(GL_READ_FRAMEBUFFER,old_read);
    glViewport(old_view[0],old_view[1],old_view[2],old_view[3]);glColorMask(old_mask[0],old_mask[1],old_mask[2],old_mask[3]);glPolygonMode(GL_FRONT_AND_BACK,old_polygon[0]);
    for(unsigned i=0;i<9;i++)if(enabled[i])glEnable(caps[i]);else glDisable(caps[i]);
    if(!ok){failed=1;for(unsigned i=0;i<8 && glGetError()!=GL_NO_ERROR;i++){}fprintf(stderr,"[GL] depth export unavailable; retaining direct readback\n");}
    return ok;
}

/* Validate the exact packed representation once per GL context. No guest
 * surface is used: all24-bit depth codes and varied stencil bytes are uploaded
 * to a temporary texture. Preserve pixel-store and PBO state because this can
 * run from either the direct or batched transfer path. */
static int export_precision(void) {
    const unsigned side=512,count=side*side;
    uint32_t *input=malloc((size_t)count*4),*output=malloc((size_t)count*4);
    if(!input || !output){free(input);free(output);return 0;}
    GLint active,binding,pack,unpack,stores[12];
    const GLenum names[]={GL_PACK_ALIGNMENT,GL_PACK_ROW_LENGTH,GL_PACK_SKIP_ROWS,GL_PACK_SKIP_PIXELS,GL_PACK_SWAP_BYTES,GL_PACK_LSB_FIRST,GL_UNPACK_ALIGNMENT,GL_UNPACK_ROW_LENGTH,GL_UNPACK_SKIP_ROWS,GL_UNPACK_SKIP_PIXELS,GL_UNPACK_SWAP_BYTES,GL_UNPACK_LSB_FIRST};
    glGetIntegerv(GL_ACTIVE_TEXTURE,&active);glActiveTexture(GL_TEXTURE0);glGetIntegerv(GL_TEXTURE_BINDING_2D,&binding);
    glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING,&pack);glGetIntegerv(GL_PIXEL_UNPACK_BUFFER_BINDING,&unpack);
    glBindBuffer(GL_PIXEL_PACK_BUFFER,0);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,0);
    for(unsigned i=0;i<12;i++){glGetIntegerv(names[i],&stores[i]);glPixelStorei(names[i],i%6==0?4:0);}
    GLuint texture=0;glGenTextures(1,&texture);glBindTexture(GL_TEXTURE_2D,texture);
    glTexImage2D(GL_TEXTURE_2D,0,GL_DEPTH24_STENCIL8,side,side,0,GL_DEPTH_STENCIL,GL_UNSIGNED_INT_24_8,NULL);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
    int ok=glGetError()==GL_NO_ERROR;
    Image test={0};test.texture=texture;test.width=test.height=side;test.depth=1;
    for(unsigned batch=0;ok && batch<64;batch++) {
        for(unsigned i=0;i<count;i++){uint32_t d=batch*count+i;input[i]=(d<<8)|((d*73u+(d>>8))&255);}
        glTexSubImage2D(GL_TEXTURE_2D,0,0,0,side,side,GL_DEPTH_STENCIL,GL_UNSIGNED_INT_24_8,input);
        if(glGetError()!=GL_NO_ERROR || !export_depth(&test,output)){ok=0;break;}
        if(memcmp(input,output,(size_t)count*4)){ok=0;break;}
    }
    glBindTexture(GL_TEXTURE_2D,binding);if(texture)glDeleteTextures(1,&texture);
    glActiveTexture(active);glBindBuffer(GL_PIXEL_PACK_BUFFER,pack);glBindBuffer(GL_PIXEL_UNPACK_BUFFER,unpack);
    for(unsigned i=0;i<12;i++)glPixelStorei(names[i],stores[i]);
    if(glGetError()!=GL_NO_ERROR)ok=0;
    free(input);free(output);return ok;
}

/* Keep native guest surfaces separate from enhanced raster targets. A color
 * resolve averages coverage; depth/stencil use the required nearest resolve.
 * Always restore both framebuffer bindings and scissor state. */
static void image_blit(const Image *im,int upload) {
    static GLuint source,target;
    if(!source){glGenFramebuffers(1,&source);glGenFramebuffers(1,&target);}
    GLint old_read,old_draw;glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&old_read);
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&old_draw);
    GLboolean scissor=glIsEnabled(GL_SCISSOR_TEST);glDisable(GL_SCISSOR_TEST);
    GLenum attachment=im->depth?GL_DEPTH_STENCIL_ATTACHMENT:GL_COLOR_ATTACHMENT0;
    glBindFramebuffer(GL_FRAMEBUFFER,source);
    glDrawBuffer(im->depth?GL_NONE:GL_COLOR_ATTACHMENT0);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER,attachment,GL_TEXTURE_2D,upload?im->native_texture:im->texture,0);
    glReadBuffer(im->depth?GL_NONE:GL_COLOR_ATTACHMENT0);
    glBindFramebuffer(GL_FRAMEBUFFER,target);
    glReadBuffer(im->depth?GL_NONE:GL_COLOR_ATTACHMENT0);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER,attachment,GL_TEXTURE_2D,upload?im->texture:im->native_texture,0);
    glDrawBuffer(im->depth?GL_NONE:GL_COLOR_ATTACHMENT0);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,source);
    unsigned from=upload?1:im->scale,to=upload?im->scale:1;
    glBlitFramebuffer(0,0,im->width*from,im->height*from,0,0,im->width*to,im->height*to,
        im->depth?(GL_DEPTH_BUFFER_BIT|GL_STENCIL_BUFFER_BIT):GL_COLOR_BUFFER_BIT,
        im->depth || upload?GL_NEAREST:GL_LINEAR);
    glFramebufferTexture2D(GL_READ_FRAMEBUFFER,attachment,GL_TEXTURE_2D,0,0);
    glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER,attachment,GL_TEXTURE_2D,0,0);
    glBindFramebuffer(GL_READ_FRAMEBUFFER,old_read);glBindFramebuffer(GL_DRAW_FRAMEBUFFER,old_draw);
    if(scissor)glEnable(GL_SCISSOR_TEST);
}

static void read_image(const Image *im,void *destination) {
    Image native;
    if(im->scale>1) {
        image_blit(im,0);native=*im;native.texture=im->native_texture;native.scale=1;
        glBindTexture(GL_TEXTURE_2D,native.texture);im=&native;
    }
    if(im->depth && export_depth(im,destination))return;
    static GLuint read_fbo;
    static int enabled=-1;
    if(enabled<0){const char *v=getenv("SSX_GL_READ_PIXELS");enabled=!(v && !strcmp(v,"0"));}
    if(enabled) {
        GLint previous_read;
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&previous_read);
        if(!read_fbo) {
            GLint previous_draw;
            glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&previous_draw);
            glGenFramebuffers(1,&read_fbo);
            if(read_fbo) {
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER,read_fbo);
                glDrawBuffer(GL_NONE);
                glBindFramebuffer(GL_DRAW_FRAMEBUFFER,previous_draw);
            }
        }
        int ok=0;
        if(read_fbo) {
            glBindFramebuffer(GL_READ_FRAMEBUFFER,read_fbo);
            GLenum attachment=im->depth?GL_DEPTH_STENCIL_ATTACHMENT:GL_COLOR_ATTACHMENT0;
            glFramebufferTexture2D(GL_READ_FRAMEBUFFER,attachment,GL_TEXTURE_2D,im->texture,0);
            glReadBuffer(im->depth?GL_NONE:GL_COLOR_ATTACHMENT0);
            if(glCheckFramebufferStatus(GL_READ_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE) {
                glReadPixels(0,0,im->width,im->height,im->depth?GL_DEPTH_STENCIL:GL_BGRA,
                    im->depth?GL_UNSIGNED_INT_24_8:GL_UNSIGNED_BYTE,destination);
                ok=glGetError()==GL_NO_ERROR;
            }
            /* Do not keep evicted texture storage alive through this FBO. */
            glFramebufferTexture2D(GL_READ_FRAMEBUFFER,attachment,GL_TEXTURE_2D,0,0);
        }
        glBindFramebuffer(GL_READ_FRAMEBUFFER,previous_read);
        if(ok)return;
        for(unsigned i=0;i<8 && glGetError()!=GL_NO_ERROR;i++){}
        enabled=0;
        fprintf(stderr,"[GL] framebuffer readback unavailable; using texture retrieval\n");
    }
    glGetTexImage(GL_TEXTURE_2D,0,im->depth?GL_DEPTH_STENCIL:GL_BGRA,
        im->depth?GL_UNSIGNED_INT_24_8:GL_UNSIGNED_BYTE,destination);
}

static int sync_batched(size_t offsets[32],size_t bytes) {
    static GLuint pack_buffer;
    static size_t capacity;
    static int failed;
    if(failed)return 0;
    if(!pack_buffer)glGenBuffers(1,&pack_buffer);
    glBindBuffer(GL_PIXEL_PACK_BUFFER,pack_buffer);
    if(bytes>capacity) {
        glBufferData(GL_PIXEL_PACK_BUFFER,bytes,NULL,GL_STREAM_READ);
        if(glGetError()!=GL_NO_ERROR)goto fallback;
        capacity=bytes;
    }
    glPixelStorei(GL_PACK_ROW_LENGTH,0);
    for(unsigned i=0;i<32;i++) {
        Image *im=&images[i];if(!im->dirty)continue;
        glBindTexture(GL_TEXTURE_2D,im->texture);
        read_image(im,(void *)(uintptr_t)offsets[i]);
    }
    if(glGetError()!=GL_NO_ERROR)goto fallback;
    const unsigned char *mapped=glMapBufferRange(GL_PIXEL_PACK_BUFFER,0,bytes,GL_MAP_READ_BIT);
    if(!mapped)goto fallback;
    for(unsigned i=0;i<32;i++) {
        Image *im=&images[i];if(!im->dirty)continue;
        for(unsigned y=0;y<im->height;y++)
            memcpy((unsigned char *)im->address+(size_t)y*im->pitch,
                mapped+offsets[i]+(size_t)y*im->width*4,(size_t)im->width*4);
    }
    if(!glUnmapBuffer(GL_PIXEL_PACK_BUFFER))goto fallback;
    glBindBuffer(GL_PIXEL_PACK_BUFFER,0);
    for(unsigned i=0;i<32;i++)images[i].dirty=0;
    return 1;
fallback:
    /* Leave every image dirty: even a failed unmap must re-read all pixels. */
    glBindBuffer(GL_PIXEL_PACK_BUFFER,0);
    for(unsigned i=0;i<8 && glGetError()!=GL_NO_ERROR;i++){}
    failed=1;
    fprintf(stderr,"[GL] batched readback unavailable; using direct transfers\n");
    return 0;
}
void ssx_gl_sync(void) {
    if(initialized!=1 || !context_current())return;
    static int batch=-1;
    if(batch<0){const char *v=getenv("SSX_GL_READBACK_BATCH");batch=!(v && !strcmp(v,"0"));}
    if(batch) {
        size_t offsets[32],bytes=0;
        unsigned count=0;
        for(unsigned i=0;i<32;i++)if(images[i].dirty) {
            offsets[i]=bytes;bytes+=(size_t)images[i].width*images[i].height*4;count++;
        }
        /* One retained buffer is bounded independently of cached GL images. */
        if(count>1 && bytes<=64u*1024u*1024u && sync_batched(offsets,bytes))return;
    }
    for(unsigned i=0;i<32;i++) {
        Image *im=&images[i];if(!im->dirty)continue;
        glBindTexture(GL_TEXTURE_2D,im->texture);glPixelStorei(GL_PACK_ROW_LENGTH,im->pitch/4);
        read_image(im,im->address);
        im->dirty=0;
    }
    glPixelStorei(GL_PACK_ROW_LENGTH,0);
}
void ssx_gl_invalidate(void) {
    if(initialized!=1)return;
    uniform_state_valid=0;
    for(unsigned i=0;i<32;i++){images[i].preserve=images[i].loaded;images[i].loaded=0;}
}
static Image *surface(void *address,unsigned pitch,unsigned width,unsigned height,unsigned depth) {
    Image *im=NULL;
    for(unsigned i=0;i<32;i++) {
        Image *x=&images[i];
        if(x->dirty && x->address==address && (x->pitch!=pitch || x->width!=width || x->height!=height || x->depth!=depth))ssx_gl_sync();
    }
    for(unsigned i=0;i<32;i++) {
        Image *x=&images[i];
        if(x->address==address && x->pitch==pitch && x->width==width && x->height==height && x->depth==depth){im=x;break;}
        if(!x->address && !im)im=x;
    }
    if(!im){
        ssx_gl_sync();im=&images[0];
        for(unsigned i=1;i<32;i++)if(images[i].used<im->used)im=&images[i];
        glDeleteTextures(1,&im->texture);if(im->native_texture)glDeleteTextures(1,&im->native_texture);framebuffer_valid=0;memset(im,0,sizeof(*im));
    }
    if(!im->address){im->address=address;im->pitch=pitch;im->width=width;im->height=height;im->depth=depth;im->scale=width*render_scale<=4096 && height*render_scale<=4096?render_scale:1;glGenTextures(1,&im->texture);if(im->scale>1)glGenTextures(1,&im->native_texture);}
    im->used=++serial;
    glBindTexture(GL_TEXTURE_2D,im->texture);
    if(!im->loaded) {
        framebuffer_valid=0;
        if(im->scale>1) {
            glTexImage2D(GL_TEXTURE_2D,0,depth?GL_DEPTH24_STENCIL8:GL_RGBA8,width*im->scale,height*im->scale,0,
                depth?GL_DEPTH_STENCIL:GL_BGRA,depth?GL_UNSIGNED_INT_24_8:GL_UNSIGNED_BYTE,NULL);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
            glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,0);
            glBindTexture(GL_TEXTURE_2D,im->native_texture);
        }
        glPixelStorei(GL_UNPACK_ROW_LENGTH,pitch/4);
        glTexImage2D(GL_TEXTURE_2D,0,depth?GL_DEPTH24_STENCIL8:GL_RGBA8,width,height,0,depth?GL_DEPTH_STENCIL:GL_BGRA,depth?GL_UNSIGNED_INT_24_8:GL_UNSIGNED_BYTE,address);
        glPixelStorei(GL_UNPACK_ROW_LENGTH,0);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_NEAREST);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,0);
        if(im->scale>1){image_blit(im,1);glBindTexture(GL_TEXTURE_2D,im->texture);}
        im->loaded=1;im->preserve=0;
    }
    return im;
}
/* Exact source comparisons replace the serial hash on every draw. Keep the
 * copies bounded, independent of the host driver's texture allocations. Losing
 * a copy only forces a fresh upload; it never hides guest texture changes. */
#define TEXTURE_COPY_BUDGET (64u * 1024u * 1024u)
static size_t texture_copy_bytes;
static void forget_texture_copy(Texture *t) {
    if(t->source_copy) {
        texture_copy_bytes-=t->bytes;
        free(t->source_copy);t->source_copy=NULL;
    }
}
static void remember_texture_source(Texture *slot,const SsxGlTexture *t) {
    forget_texture_copy(slot);
    if(t->source_size>TEXTURE_COPY_BUDGET)return;
    while(texture_copy_bytes>TEXTURE_COPY_BUDGET-t->source_size) {
        Texture *oldest=NULL;
        for(unsigned i=0;i<256;i++)
            if(textures[i].source_copy && (!oldest || textures[i].used<oldest->used))
                oldest=&textures[i];
        if(!oldest)break;
        forget_texture_copy(oldest);
    }
    slot->source_copy=malloc(t->source_size);
    if(slot->source_copy) {
        memcpy(slot->source_copy,t->source,t->source_size);
        texture_copy_bytes+=t->source_size;
    }
}
static int wrap(unsigned mode){return mode==1?GL_REPEAT:mode==2?GL_MIRRORED_REPEAT:GL_CLAMP_TO_EDGE;}
static GLuint texture(const SsxGlTexture *t) {
    if(!t->valid || !t->width || !t->height || !t->source || !t->source_size || t->width>4096 || t->height>4096)return 0;
    /* Rendered surfaces are authoritative until synchronized to guest RAM. */
    for(unsigned i=0;i<32;i++)if(images[i].address==t->source && !images[i].depth && images[i].loaded && images[i].width==t->width && images[i].height==t->height)return images[i].texture;
    unsigned levels=t->levels && t->sample_level?t->levels:1;
    if(levels>13)return 0;
    Texture *slot=NULL,*oldest=&textures[0];
    for(unsigned i=0;i<256;i++) {
        Texture *x=&textures[i];if(x->used<oldest->used)oldest=x;
        if(x->address==t->source && x->width==t->width && x->height==t->height && x->format==t->format && x->pitch==t->pitch){slot=x;break;}
    }
    if(!slot)slot=oldest;
    if(!slot->texture)glGenTextures(1,&slot->texture);
    glBindTexture(GL_TEXTURE_2D,slot->texture);
    if(slot->address!=t->source || slot->bytes!=t->source_size || slot->width!=t->width || slot->height!=t->height || slot->format!=t->format || slot->pitch!=t->pitch || slot->levels!=levels || !slot->source_copy || memcmp(slot->source_copy,t->source,t->source_size)) {
        uint32_t *pixels=malloc((size_t)t->width*t->height*4);if(!pixels)return 0;
        unsigned width=t->width,height=t->height;
        for(unsigned level=0;level<levels;level++) {
            for(unsigned y=0;y<height;y++)for(unsigned x=0;x<width;x++) {
                uint32_t c=0xff000000;
                if(t->sample_level)t->sample_level(t->opaque,level,x,y,&c);
                else t->sample(t->opaque,x,y,&c);
                pixels[y*width+x]=c;
            }
            glTexImage2D(GL_TEXTURE_2D,level,GL_RGBA8,width,height,0,GL_BGRA,GL_UNSIGNED_BYTE,pixels);
            if(width>1)width>>=1;if(height>1)height>>=1;
        }
        free(pixels);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAX_LEVEL,levels-1);
        remember_texture_source(slot,t);
        slot->address=t->source;slot->bytes=t->source_size;slot->width=t->width;slot->height=t->height;slot->format=t->format;slot->pitch=t->pitch;slot->levels=levels;
    }
    slot->used=++serial;
    return slot->texture;
}
/* Program constants let GLSL/LLVM remove unused stages and register-array
 * indexing. Material colors remain uniforms, so animated colors do not compile
 * new shaders. Keep the generic program available for differential tests. */
typedef struct {
    uint32_t control,combiner[8][4],final[2],tex_mode,normalized,alpha_enable,alpha_func;
} ProgramKey;
typedef struct {ProgramKey key;GLuint object;Uniforms uniforms;unsigned used;} Program;
static Program programs[128];
static GLuint generic_program;
static Uniforms generic_uniforms;
static char *replace_source(char *source,const char *from,const char *to) {
    char *at=strstr(source,from);if(!at)abort();
    size_t prefix=(size_t)(at-source),tail=strlen(at+strlen(from));
    char *out=malloc(prefix+strlen(to)+tail+1);if(!out)abort();
    memcpy(out,source,prefix);memcpy(out+prefix,to,strlen(to));
    memcpy(out+prefix+strlen(to),at+strlen(from),tail+1);free(source);return out;
}
static void select_program(const SsxGlDraw *d) {
    if(!generic_program){generic_program=program;generic_uniforms=uniforms;}
    static int specialize=-1;
    if(specialize<0){const char *option=getenv("SSX_GL_SPECIALIZE");specialize=!(option && !strcmp(option,"0"));}
    if(!specialize){program=generic_program;uniforms=generic_uniforms;return;}
    const uint32_t *r=d->registers;ProgramKey key={0};key.control=r[0x1e60/4];
    unsigned count=key.control&15;if(count>8)count=8;
    for(unsigned i=0;i<count;i++) {
        key.combiner[i][0]=r[0xac0/4+i];key.combiner[i][1]=r[0x260/4+i];
        key.combiner[i][2]=r[0x1e40/4+i];key.combiner[i][3]=r[0xaa0/4+i];
    }
    if(count){key.final[0]=r[0x288/4];key.final[1]=r[0x28c/4];key.tex_mode=r[0x1e70/4];}
    else key.tex_mode=1;
    for(int i=0;i<4;i++)if(count && d->textures[i].normalized)key.normalized|=1u<<i;
    key.alpha_enable=d->alpha_enable;key.alpha_func=d->alpha_enable?d->alpha_func:0;
    Program *slot=NULL,*oldest=&programs[0];
    for(unsigned i=0;i<128;i++) {
        Program *x=&programs[i];if(x->used<oldest->used)oldest=x;
        if(x->object && !memcmp(&x->key,&key,sizeof key)){slot=x;break;}
    }
    if(!slot) {
        slot=oldest;char *source=strdup(ssx_gl_fragment_source),value[2048];if(!source)abort();
        snprintf(value,sizeof value,"const uint control=%uu;",key.control);
        source=replace_source(source,"uniform uint control;",value);
        size_t used=(size_t)snprintf(value,sizeof value,"const uvec4 combiner[8]=uvec4[8](");
        for(int i=0;i<8;i++)used+=(size_t)snprintf(value+used,sizeof(value)-used,"%suvec4(%uu,%uu,%uu,%uu)",i?",":"",key.combiner[i][0],key.combiner[i][1],key.combiner[i][2],key.combiner[i][3]);
        snprintf(value+used,sizeof(value)-used,");");
        source=replace_source(source,"uniform uvec4 combiner[8];",value);
        snprintf(value,sizeof value,"const uvec2 finalInput=uvec2(%uu,%uu);",key.final[0],key.final[1]);
        source=replace_source(source,"uniform uvec2 finalInput;",value);
        snprintf(value,sizeof value,"const ivec4 texMode=ivec4(%u,%u,%u,%u);uniform ivec4 texValid;const ivec4 texNormalized=ivec4(%u,%u,%u,%u);",key.tex_mode&31,(key.tex_mode>>5)&31,(key.tex_mode>>10)&31,(key.tex_mode>>15)&31,key.normalized&1,(key.normalized>>1)&1,(key.normalized>>2)&1,(key.normalized>>3)&1);
        source=replace_source(source,"uniform ivec4 texMode,texValid,texNormalized;",value);
        snprintf(value,sizeof value,"const int alphaEnable=%u,alphaFunc=%u;uniform int alphaRef;",key.alpha_enable,key.alpha_func);
        source=replace_source(source,"uniform int alphaEnable,alphaFunc,alphaRef;",value);
        GLuint vs=shader(GL_VERTEX_SHADER,vertex_source),fs=shader(GL_FRAGMENT_SHADER,source);free(source);
        if(!vs||!fs)abort();GLuint object=glCreateProgram();glAttachShader(object,vs);glAttachShader(object,fs);glLinkProgram(object);glDeleteShader(vs);glDeleteShader(fs);
        GLint ok;glGetProgramiv(object,GL_LINK_STATUS,&ok);if(!ok){char log[4096];glGetProgramInfoLog(object,sizeof log,NULL,log);fprintf(stderr,"[GL] specialized link: %s\n",log);abort();}
        if(slot->object)glDeleteProgram(slot->object);
        slot->object=object;slot->key=key;slot->uniforms=program_uniforms(object);
        uniform_state_valid=0;
    }
    slot->used=++serial;program=slot->object;uniforms=slot->uniforms;
}

static void unpack(uint32_t c,float *v){v[0]=((c>>16)&255)/255.f;v[1]=((c>>8)&255)/255.f;v[2]=(c&255)/255.f;v[3]=(c>>24)/255.f;}
/* Uniform values belong to a linked program, not to the current texture or
 * render target. Compare only active locations: specialization removes many
 * uniforms altogether. Raw color words defer unpacking until an upload is
 * needed. A program switch, relink or explicit invalidation forces an upload. */
static void draw_uniforms(const SsxGlDraw *d,const GLint modes[4],
                          const GLint valid[4],const GLint normalized[4],const float sizes[8]) {
    typedef struct {
        float extent[2],sizes[8];
        GLint modes[4],valid[4],normalized[4],alpha_enable,alpha_func,alpha_ref,legacy;
        GLint window_exclusive,window_rect[32];
        uint32_t control,combiner[32],constant0[8],constant1[8],final0,final1,final_input[2];
    } Values;
    static Values previous;
    static GLuint previous_program;
    static int enabled=-1;
    if(enabled<0){const char *v=getenv("SSX_GL_UNIFORM_CACHE");enabled=!(v && !strcmp(v,"0"));}
    int cache=enabled && state_cache_enabled();
    Values value;memset(&value,0,sizeof value);const uint32_t *r=d->registers;
    value.window_exclusive=r[0x2b4/4]&1;
    for(unsigned i=0;i<8;i++) {
        uint32_t h=r[0x2c0/4+i],v=r[0x2e0/4+i];
        value.window_rect[4*i]=h&0xfff;value.window_rect[4*i+1]=v&0xfff;
        value.window_rect[4*i+2]=(h>>16)&0xfff;value.window_rect[4*i+3]=(v>>16)&0xfff;
    }
    if(uniforms.extent>=0){value.extent[0]=d->width;value.extent[1]=d->height;}
    if(uniforms.texMode>=0)memcpy(value.modes,modes,sizeof value.modes);
    if(uniforms.texValid>=0)memcpy(value.valid,valid,sizeof value.valid);
    if(uniforms.texNormalized>=0)memcpy(value.normalized,normalized,sizeof value.normalized);
    if(uniforms.texSize>=0)memcpy(value.sizes,sizes,sizeof value.sizes);
    if(uniforms.control>=0)value.control=r[0x1e60/4];
    if(uniforms.combiner>=0)for(unsigned i=0;i<8;i++) {
        value.combiner[i*4]=r[0xac0/4+i];value.combiner[i*4+1]=r[0x260/4+i];
        value.combiner[i*4+2]=r[0x1e40/4+i];value.combiner[i*4+3]=r[0xaa0/4+i];
    }
    if(uniforms.constant0>=0)memcpy(value.constant0,r+0xa60/4,sizeof value.constant0);
    if(uniforms.constant1>=0)memcpy(value.constant1,r+0xa80/4,sizeof value.constant1);
    if(uniforms.final0>=0)value.final0=r[0x1e20/4];
    if(uniforms.final1>=0)value.final1=r[0x1e24/4];
    if(uniforms.finalInput>=0){value.final_input[0]=r[0x288/4];value.final_input[1]=r[0x28c/4];}
    if(uniforms.alphaEnable>=0)value.alpha_enable=d->alpha_enable;
    if(uniforms.alphaFunc>=0)value.alpha_func=d->alpha_func;
    if(uniforms.alphaRef>=0)value.alpha_ref=d->alpha_ref&255;
    if(uniforms.legacyTextured>=0)value.legacy=valid[0];
    if(cache && uniform_state_valid && previous_program==program && !memcmp(&previous,&value,sizeof value))return;
    int partial=cache && uniform_state_valid && previous_program==program;
#define SSX_UNIFORM_CHANGED(field) (!partial || memcmp(&previous.field,&value.field,sizeof value.field))
    if(uniforms.windowExclusive>=0 && SSX_UNIFORM_CHANGED(window_exclusive))glUniform1i(uniforms.windowExclusive,value.window_exclusive);
    if(uniforms.windowRect>=0 && SSX_UNIFORM_CHANGED(window_rect)){int rect[32];for(unsigned i=0;i<32;i++)rect[i]=(i%4<2)?value.window_rect[i]*draw_scale:(value.window_rect[i]+1)*draw_scale-1;glUniform4iv(uniforms.windowRect,8,rect);}
    if(uniforms.extent>=0 && SSX_UNIFORM_CHANGED(extent))glUniform2fv(uniforms.extent,1,value.extent);
    if(uniforms.texMode>=0 && SSX_UNIFORM_CHANGED(modes))glUniform4iv(uniforms.texMode,1,value.modes);
    if(uniforms.texValid>=0 && SSX_UNIFORM_CHANGED(valid))glUniform4iv(uniforms.texValid,1,value.valid);
    if(uniforms.texNormalized>=0 && SSX_UNIFORM_CHANGED(normalized))glUniform4iv(uniforms.texNormalized,1,value.normalized);
    if(uniforms.texSize>=0 && SSX_UNIFORM_CHANGED(sizes))glUniform2fv(uniforms.texSize,4,value.sizes);
    if(uniforms.control>=0 && SSX_UNIFORM_CHANGED(control))glUniform1ui(uniforms.control,value.control);
    if(uniforms.combiner>=0 && SSX_UNIFORM_CHANGED(combiner))glUniform4uiv(uniforms.combiner,8,value.combiner);
    float color[32];
    if(uniforms.constant0>=0 && SSX_UNIFORM_CHANGED(constant0)){for(unsigned i=0;i<8;i++)unpack(value.constant0[i],color+i*4);glUniform4fv(uniforms.constant0,8,color);}
    if(uniforms.constant1>=0 && SSX_UNIFORM_CHANGED(constant1)){for(unsigned i=0;i<8;i++)unpack(value.constant1[i],color+i*4);glUniform4fv(uniforms.constant1,8,color);}
    if(uniforms.final0>=0 && SSX_UNIFORM_CHANGED(final0)){unpack(value.final0,color);glUniform4fv(uniforms.final0,1,color);}
    if(uniforms.final1>=0 && SSX_UNIFORM_CHANGED(final1)){unpack(value.final1,color);glUniform4fv(uniforms.final1,1,color);}
    if(uniforms.finalInput>=0 && SSX_UNIFORM_CHANGED(final_input))glUniform2uiv(uniforms.finalInput,1,value.final_input);
    if(uniforms.alphaEnable>=0 && SSX_UNIFORM_CHANGED(alpha_enable))glUniform1i(uniforms.alphaEnable,value.alpha_enable);
    if(uniforms.alphaFunc>=0 && SSX_UNIFORM_CHANGED(alpha_func))glUniform1i(uniforms.alphaFunc,value.alpha_func);
    if(uniforms.alphaRef>=0 && SSX_UNIFORM_CHANGED(alpha_ref))glUniform1i(uniforms.alphaRef,value.alpha_ref);
    if(uniforms.legacyTextured>=0 && SSX_UNIFORM_CHANGED(legacy))glUniform1i(uniforms.legacyTextured,value.legacy);

#undef SSX_UNIFORM_CHANGED
    memcpy(&previous,&value,sizeof value);previous_program=program;uniform_state_valid=1;
}
int ssx_gl_draw(const SsxGlDraw *d,const SsxGlVertex *v,unsigned count) {
    if(!count || !ssx_gl_enabled())return 0;
    if(!d->color || !d->width || !d->height || d->width>4096 || d->height>4096 || d->pitch<d->width*4)return 0;
    select_program(d);
    const uint32_t *r=d->registers;
    Image *color=surface(d->color,d->pitch,d->width,d->height,0);
    Image *depth=d->depth?surface(d->depth,d->depth_pitch,d->width,d->height,1):NULL;
    if(!color || (d->depth && !depth))return 0;
    int cache=state_cache_enabled();
    if(!cache || !framebuffer_valid || attached_color!=color->texture || attached_depth!=(depth?depth->texture:0)) {
        glBindFramebuffer(GL_FRAMEBUFFER,fbo);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_TEXTURE_2D,color->texture,0);
        glFramebufferTexture2D(GL_FRAMEBUFFER,GL_DEPTH_STENCIL_ATTACHMENT,GL_TEXTURE_2D,depth?depth->texture:0,0);
        if(glCheckFramebufferStatus(GL_FRAMEBUFFER)!=GL_FRAMEBUFFER_COMPLETE){fprintf(stderr,"[GL] incomplete framebuffer\n");return 0;}
        attached_color=color->texture;attached_depth=depth?depth->texture:0;framebuffer_valid=1;
    }
    uint32_t raster_state[]={d->width,d->height,d->clip_x,d->clip_y,d->clip_w,d->clip_h,
        d->depth_enable && depth,d->depth_write,d->depth_func,d->color_mask,
        d->blend_enable,d->blend_src,d->blend_dst,d->blend_equation};
    static uint32_t previous_raster[14];static int raster_valid;
    if(!cache || !raster_valid || memcmp(previous_raster,raster_state,sizeof raster_state)) {
        glViewport(0,0,d->width*color->scale,d->height*color->scale);glEnable(GL_SCISSOR_TEST);glScissor(d->clip_x*color->scale,d->clip_y*color->scale,d->clip_w*color->scale,d->clip_h*color->scale);
        if(d->depth_enable && depth)glEnable(GL_DEPTH_TEST);else glDisable(GL_DEPTH_TEST);
        glDepthMask(d->depth_write!=0);glDepthFunc(GL_NEVER+(d->depth_func&7));
        glColorMask((d->color_mask&0xff0000)!=0,(d->color_mask&0xff00)!=0,(d->color_mask&0xff)!=0,(d->color_mask&0xff000000)!=0);
        if(d->blend_enable)glEnable(GL_BLEND);else glDisable(GL_BLEND);
        glBlendFunc(d->blend_src,d->blend_dst);glBlendEquation(d->blend_equation?d->blend_equation:GL_FUNC_ADD);
        /* CPU stage has already applied guest culling and homogeneous clipping. */
        glDisable(GL_CULL_FACE);
        memcpy(previous_raster,raster_state,sizeof raster_state);raster_valid=1;
    }
    glUseProgram(program);
    GLint modes[4],valid[4],normalized[4];float sizes[8];
    for(int i=0;i<4;i++) {
        const SsxGlTexture *t=&d->textures[i];glActiveTexture(GL_TEXTURE0+i);
        modes[i]=(r[0x1e70/4]>>(i*5))&31;
        GLuint id=(modes[i]==1 || (!(r[0x1e60/4]&15) && i==0))?texture(t):0;glBindTexture(GL_TEXTURE_2D,id);
        glBindSampler(i,samplers[i]);
        if(id){
            int min_linear=t->min_filter==2 || t->min_filter==4 || t->min_filter==6 || t->min_filter==7;
            GLint filter=min_linear?GL_LINEAR:GL_NEAREST;
            if(t->levels>1 && t->sample_level) {
                switch(t->min_filter) {
                    case 3:filter=GL_NEAREST_MIPMAP_NEAREST;break;
                    case 4:filter=GL_LINEAR_MIPMAP_NEAREST;break;
                    case 5:filter=GL_NEAREST_MIPMAP_LINEAR;break;
                    case 6:filter=GL_LINEAR_MIPMAP_LINEAR;break;
                }
            }
            struct SamplerState { GLint filter,mag,u,v;float bias,min,max; };
            struct SamplerState state={filter,t->mag_filter==2?GL_LINEAR:GL_NEAREST,
                wrap(t->wrap_u),wrap(t->wrap_v),t->lod_bias,t->min_lod,t->levels>1?t->max_lod:0};
            static struct SamplerState previous[4];static int valid_sampler[4];
            if(!cache || !valid_sampler[i] || memcmp(&previous[i],&state,sizeof state)) {
                glSamplerParameteri(samplers[i],GL_TEXTURE_MIN_FILTER,state.filter);
                glSamplerParameteri(samplers[i],GL_TEXTURE_MAG_FILTER,state.mag);
                glSamplerParameteri(samplers[i],GL_TEXTURE_WRAP_S,state.u);
                glSamplerParameteri(samplers[i],GL_TEXTURE_WRAP_T,state.v);
                glSamplerParameterf(samplers[i],GL_TEXTURE_LOD_BIAS,state.bias);
                glSamplerParameterf(samplers[i],GL_TEXTURE_MIN_LOD,state.min);
                glSamplerParameterf(samplers[i],GL_TEXTURE_MAX_LOD,state.max);
                previous[i]=state;valid_sampler[i]=1;
            }
        }
        modes[i]=(r[0x1e70/4]>>(i*5))&31;valid[i]=id!=0;normalized[i]=t->normalized;sizes[i*2]=t->width?t->width:1;sizes[i*2+1]=t->height?t->height:1;
    }
    if(!(r[0x1e60/4]&15)){modes[0]=1;normalized[0]=0;}
    if(draw_scale!=color->scale){draw_scale=color->scale;uniform_state_valid=0;}
    draw_uniforms(d,modes,valid,normalized,sizes);
    glBindVertexArray(vao);
    GLuint source=geometry_buffer?geometry_buffer:vbo;
    glBindBuffer(GL_ARRAY_BUFFER,source);
    if(bound_vertex_buffer!=source) {
        for(int i=0;i<7;i++)glVertexAttribPointer(i,4,GL_FLOAT,GL_FALSE,sizeof(SsxGlVertex),(void*)(uintptr_t)(i*16));
        bound_vertex_buffer=source;
    }
    if(!geometry_buffer)glBufferData(GL_ARRAY_BUFFER,(size_t)count*sizeof(*v),v,GL_STREAM_DRAW);
    if(zpass_query && zpass_scale!=color->scale)zpass_finish(1);
    if(zpass_enabled && !zpass_query) {
        zpass_scale=color->scale;
        glGenQueries(1,&zpass_query);glBeginQuery(GL_SAMPLES_PASSED,zpass_query);
    }
    if(geometry_buffer)glDrawTransformFeedback(GL_TRIANGLES,geometry_feedback);
    else glDrawArrays(GL_TRIANGLES,0,count);
    color->dirty=1;if(depth && d->depth_enable && d->depth_write)depth->dirty=1;
    for(unsigned i=0;i<32;i++) {
        Image *other=&images[i];
        if(other!=color && other->address==color->address){other->loaded=0;other->preserve=0;}
        if(depth && other!=depth && other->address==depth->address){other->loaded=0;other->preserve=0;}
    }
    GLenum error=glGetError();if(error){fprintf(stderr,"[GL] draw error %x\n",error);abort();}
    return 1;
}
const uint32_t *ssx_gl_present(const void *address,unsigned pitch,unsigned *width,unsigned *height) {
    if(initialized!=1 || render_scale==1 || !context_current())return NULL;
    static uint32_t *pixels;static size_t capacity;
    for(unsigned i=0;i<32;i++) {
        Image *im=&images[i];if(im->address!=address || im->pitch!=pitch || im->depth || !im->loaded || im->scale==1)continue;
        size_t bytes=(size_t)im->width*im->height*im->scale*im->scale*4;
        if(bytes>64u*1024u*1024u)return NULL;
        if(bytes>capacity){void *next=realloc(pixels,bytes);if(!next)return NULL;pixels=next;capacity=bytes;}
        Image full=*im;full.width*=im->scale;full.height*=im->scale;full.scale=1;
        glBindTexture(GL_TEXTURE_2D,im->texture);glPixelStorei(GL_PACK_ROW_LENGTH,0);
        read_image(&full,pixels);if(glGetError()!=GL_NO_ERROR)return NULL;
        *width=full.width;*height=full.height;return pixels;
    }
    return NULL;
}
/* CPU clear semantics, including partial channels and depth/stencil masks,
 * mirrored into retained enhanced targets. Called only after the native clear. */
void ssx_gl_clear_surface(void *address,unsigned pitch,unsigned depth,unsigned x0,unsigned y0,unsigned x1,unsigned y1,uint32_t value,uint32_t mask) {
    if(initialized!=1 || render_scale==1 || !context_current())return;
    for(unsigned i=0;i<32;i++) {
        Image *im=&images[i];if(im->address!=address || im->pitch!=pitch || im->depth!=depth || !im->preserve || im->scale==1 || x1>im->width || y1>im->height)continue;
        static GLuint clear_fbo;if(!clear_fbo)glGenFramebuffers(1,&clear_fbo);
        GLint old_draw,old_read,rect[4],stencil,back_stencil;GLboolean colors[4],depth_mask,scissor=glIsEnabled(GL_SCISSOR_TEST);
        GLfloat old_color[4];GLdouble old_depth;GLint old_stencil;
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING,&old_draw);glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING,&old_read);glGetIntegerv(GL_SCISSOR_BOX,rect);
        glGetBooleanv(GL_COLOR_WRITEMASK,colors);glGetBooleanv(GL_DEPTH_WRITEMASK,&depth_mask);glGetIntegerv(GL_STENCIL_WRITEMASK,&stencil);glGetIntegerv(GL_STENCIL_BACK_WRITEMASK,&back_stencil);
        glGetFloatv(GL_COLOR_CLEAR_VALUE,old_color);glGetDoublev(GL_DEPTH_CLEAR_VALUE,&old_depth);glGetIntegerv(GL_STENCIL_CLEAR_VALUE,&old_stencil);
        glBindFramebuffer(GL_FRAMEBUFFER,clear_fbo);
        GLenum attachment=depth?GL_DEPTH_STENCIL_ATTACHMENT:GL_COLOR_ATTACHMENT0;
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER,attachment,GL_TEXTURE_2D,im->texture,0);glDrawBuffer(depth?GL_NONE:GL_COLOR_ATTACHMENT0);glReadBuffer(depth?GL_NONE:GL_COLOR_ATTACHMENT0);
        glEnable(GL_SCISSOR_TEST);glScissor(x0*im->scale,y0*im->scale,(x1-x0)*im->scale,(y1-y0)*im->scale);
        if(depth){glDepthMask(GL_TRUE);glStencilMask(255);glClearDepth((double)(value>>8)/16777215.0);glClearStencil(value&255);glClear(((mask&0xffffff00u)?GL_DEPTH_BUFFER_BIT:0)|((mask&255)?GL_STENCIL_BUFFER_BIT:0));}
        else{glColorMask((mask&0xff0000)!=0,(mask&0xff00)!=0,(mask&255)!=0,(mask&0xff000000u)!=0);glClearColor(((value>>16)&255)/255.f,((value>>8)&255)/255.f,(value&255)/255.f,(value>>24)/255.f);glClear(GL_COLOR_BUFFER_BIT);}
        glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER,attachment,GL_TEXTURE_2D,0,0);glBindFramebuffer(GL_DRAW_FRAMEBUFFER,old_draw);glBindFramebuffer(GL_READ_FRAMEBUFFER,old_read);
        glScissor(rect[0],rect[1],rect[2],rect[3]);if(!scissor)glDisable(GL_SCISSOR_TEST);
        glColorMask(colors[0],colors[1],colors[2],colors[3]);glDepthMask(depth_mask);glStencilMaskSeparate(GL_FRONT,stencil);glStencilMaskSeparate(GL_BACK,back_stencil);
        glClearColor(old_color[0],old_color[1],old_color[2],old_color[3]);glClearDepth(old_depth);glClearStencil(old_stencil);
        im->loaded=1;im->preserve=0;
    }
}

int ssx_gl_geometry_prepare(const uint32_t (*code)[4],unsigned count,const float *context,unsigned indices) {
    return ssx_gl_enabled()?ssx_gpu_geometry_prepare(code,count,context,indices):0;
}
int ssx_gl_draw_geometry(const SsxGlDraw *d,int token,const float *context,
                         const float *inputs,unsigned vertices,const uint32_t *indices,unsigned count) {
    unsigned buffer,feedback;
    if(!ssx_gpu_geometry_run(token,context,inputs,vertices,indices,count,d,&buffer,&feedback))return 0;
    geometry_buffer=buffer;geometry_feedback=feedback;
    int result=ssx_gl_draw(d,NULL,count);
    geometry_buffer=geometry_feedback=0;return result;
}

#else
int ssx_gl_geometry_prepare(const uint32_t (*c)[4],unsigned n,const float *v,unsigned i){(void)c;(void)n;(void)v;(void)i;return 0;}
int ssx_gl_draw_geometry(const SsxGlDraw*d,int t,const float*c,const float*i,unsigned v,const uint32_t*x,unsigned n){(void)d;(void)t;(void)c;(void)i;(void)v;(void)x;(void)n;return 0;}
int ssx_gl_enabled(void){return 0;}
int ssx_gl_draw(const SsxGlDraw *d,const SsxGlVertex *v,unsigned count){(void)d;(void)v;(void)count;return 0;}
const uint32_t *ssx_gl_present(const void*a,unsigned p,unsigned*w,unsigned*h){(void)a;(void)p;(void)w;(void)h;return NULL;}
void ssx_gl_clear_surface(void*a,unsigned p,unsigned d,unsigned x,unsigned y,unsigned u,unsigned v,uint32_t c,uint32_t m){(void)a;(void)p;(void)d;(void)x;(void)y;(void)u;(void)v;(void)c;(void)m;}
void ssx_gl_sync(void){}
void ssx_gl_invalidate(void){}
void ssx_gl_zpass_enable(int enabled){(void)enabled;}
void ssx_gl_zpass_clear(void){}
uint32_t ssx_gl_zpass_result(void){return 0;}
#endif
