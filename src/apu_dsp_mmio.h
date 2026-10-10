#ifndef SSX_APU_DSP_MMIO_H
#define SSX_APU_DSP_MMIO_H
#include "apu_dsp_backend.h"
/* One GP or EP 64 KiB register aperture. Backend lifetime belongs to caller.
 * Callers serialize register accesses with audio execution. */
typedef struct {
    SsxDsp *backend;
    bool gp;
    uint32_t registers[0x4000];
} SsxDspMmio;
void ssx_dsp_mmio_init(SsxDspMmio *m,SsxDsp *backend,bool gp);
bool ssx_dsp_mmio_read(SsxDspMmio *m,uint32_t offset,unsigned size,uint32_t *value);
bool ssx_dsp_mmio_write(SsxDspMmio *m,uint32_t offset,unsigned size,uint32_t value);
bool ssx_dsp_mmio_enabled(const SsxDspMmio *m);
#endif
