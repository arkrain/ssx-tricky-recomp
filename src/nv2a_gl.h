/* Optional host OpenGL rasterization of already executed NV2A vertices. */
#ifndef SSX_NV2A_GL_H
#define SSX_NV2A_GL_H
#include <stdint.h>
#include <stddef.h>
typedef struct {
    float position[4], diffuse[4], specular[4], tex[4][4];
} SsxGlVertex;
typedef struct {
    const void *source;
    size_t source_size;
    unsigned width,height,format,pitch,wrap_u,wrap_v,normalized,valid;
    const void *opaque;
    int (*sample)(const void *,int,int,uint32_t *);
    unsigned min_filter,mag_filter;
    unsigned levels;
    int (*sample_level)(const void *,unsigned,int,int,uint32_t *);
    float lod_bias,min_lod,max_lod;
} SsxGlTexture;
typedef struct {
    void *color,*depth;
    unsigned pitch,depth_pitch,width,height;
    unsigned clip_x,clip_y,clip_w,clip_h;
    unsigned color_mask,depth_enable,depth_write,depth_func;
    unsigned alpha_enable,alpha_func,alpha_ref;
    unsigned blend_enable,blend_src,blend_dst,blend_equation;
    const uint32_t *registers;
    SsxGlTexture textures[4];
} SsxGlDraw;
int ssx_gl_enabled(void);
int ssx_gl_draw(const SsxGlDraw *,const SsxGlVertex *,unsigned);
/* Complete writes before CPU consumers; invalidate after CPU surface writes. */
void ssx_gl_sync(void);
const uint32_t *ssx_gl_present(const void *,unsigned,unsigned *,unsigned *);
void ssx_gl_clear_surface(void *,unsigned,unsigned,unsigned,unsigned,unsigned,unsigned,uint32_t,uint32_t);
void ssx_gl_invalidate(void);
/* Accumulate actual fragments passing depth/alpha tests until CLEAR_REPORT. */
void ssx_gl_zpass_enable(int enabled);
void ssx_gl_zpass_clear(void);
uint32_t ssx_gl_zpass_result(void);
int ssx_gl_geometry_prepare(const uint32_t (*code)[4],unsigned count,const float *context,unsigned indices);
int ssx_gl_draw_geometry(const SsxGlDraw *draw,int token,const float *context,
                         const float *inputs,unsigned vertices,const uint32_t *indices,unsigned count);
#endif
