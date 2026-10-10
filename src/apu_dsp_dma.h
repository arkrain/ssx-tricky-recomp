#ifndef SSX_APU_DSP_DMA_H
#define SSX_APU_DSP_DMA_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
typedef uint8_t *(*SsxDspBusResolve)(void *,uint64_t,size_t);
bool ssx_dsp_sg_bus(SsxDspBusResolve resolve,void *opaque,uint32_t table,
    uint32_t max_sge,uint8_t *buffer,uint32_t address,size_t length,bool write);
bool ssx_dsp_ring_bus(SsxDspBusResolve resolve,void *opaque,uint32_t table,
    uint32_t max_sge,uint8_t *buffer,uint32_t base,uint32_t end,
    uint32_t *current,size_t length,bool write);
/* MCPX descriptors are 8 bytes, with a little-endian physical address first.
 * max_sge is inclusive; logical pages are 4096 bytes. External buffers must
 * not alias RAM. Invalid transfers leave RAM, buffer and cursor unchanged. */
bool ssx_dsp_sg_transfer(uint8_t *ram,size_t ram_size,uint32_t table,
    uint32_t max_sge,uint8_t *buffer,uint32_t address,size_t length,bool write);
bool ssx_dsp_ring_transfer(uint8_t *ram,size_t ram_size,uint32_t table,
    uint32_t max_sge,uint8_t *buffer,uint32_t base,uint32_t end,
    uint32_t *current,size_t length,bool write);
#endif
