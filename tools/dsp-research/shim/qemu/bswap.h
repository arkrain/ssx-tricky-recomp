#pragma once
#include <stdint.h>
static inline uint32_t ldl_le_p(const void *p) {
    const uint8_t *b=p;return b[0]|(uint32_t)b[1]<<8|(uint32_t)b[2]<<16|(uint32_t)b[3]<<24;
}
static inline void stl_le_p(void *p,uint32_t v) {
    uint8_t *b=p;for(unsigned i=0;i<4;i++)b[i]=(uint8_t)(v>>(i*8));
}
