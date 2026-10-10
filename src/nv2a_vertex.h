#ifndef SSX_NV2A_VERTEX_H
#define SSX_NV2A_VERTEX_H
#include <stdint.h>
int ssx_vertex_method(uint32_t method, uint32_t value);
int ssx_vertex_active(void);
uint32_t ssx_vertex_input_mask(void);
int ssx_vertex_execute(const float input[16][4], float output[13][4]);
int ssx_vertex_snapshot(const uint32_t (**code)[4], unsigned *count, const float **context);
#endif
