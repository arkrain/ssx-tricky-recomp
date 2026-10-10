#ifndef SSX_NV2A_GPU_GEOMETRY_H
#define SSX_NV2A_GPU_GEOMETRY_H
#include "nv2a_gl.h"
int ssx_gpu_geometry_prepare(const uint32_t (*code)[4],unsigned available,const float *context,unsigned indices);
int ssx_gpu_geometry_run(int token,const float *context,const float *inputs,unsigned vertices,
                        const uint32_t *indices,unsigned count,const SsxGlDraw *draw,
                        unsigned *buffer,unsigned *feedback);
#endif
