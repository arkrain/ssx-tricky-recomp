#ifndef SSX_NV2A_VERTEX_NATIVE_H
#define SSX_NV2A_VERTEX_NATIVE_H
#include <stdint.h>
typedef int (*SsxNativeVertexFn)(const float *input,const float *constants,float *output);
SsxNativeVertexFn ssx_vertex_native_find(const uint32_t (*words)[4],unsigned available);
#endif
