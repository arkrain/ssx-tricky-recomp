#ifndef SSX_APU_DSP_BACKEND_H
#define SSX_APU_DSP_BACKEND_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* Opaque boundary: the APU must not depend on the interpreter's layout. */
typedef struct SsxDsp SsxDsp;
typedef void (*SsxDspScratch)(void *,uint8_t *,uint32_t,size_t,bool);
typedef void (*SsxDspFifo)(void *,uint8_t *,unsigned,size_t,bool);
SsxDsp *ssx_dsp_create(void *opaque,SsxDspScratch scratch,SsxDspFifo fifo,bool gp);
void ssx_dsp_destroy(SsxDsp *dsp);
void ssx_dsp_reset(SsxDsp *dsp);
void ssx_dsp_bootstrap(SsxDsp *dsp);
uint32_t ssx_dsp_read(SsxDsp *dsp,char space,uint32_t address);
void ssx_dsp_write(SsxDsp *dsp,char space,uint32_t address,uint32_t value);
/* Execute a frame until the program halts or its cycle budget is exhausted.
 * A false result must be reported by the caller, not treated as completion. */
bool ssx_dsp_frame(SsxDsp *dsp,unsigned budget,unsigned *cycles);
#endif
