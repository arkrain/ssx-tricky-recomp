/* This port currently builds the pinned C interpreter only. */
#include "dsp.h"
#include <stdlib.h>
const DSPOps jit_dsp_ops={0};
void dsp_jit_init(DSPState *dsp) {(void)dsp;abort();}
