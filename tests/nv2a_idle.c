#include "kernel/nv2a_idle.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdint.h>
int main(void)
{
    unsigned polls=0;
    for(unsigned round=0;round<3;round++) {
        for(unsigned i=0;i<15;i++)assert(!nv2a_idle_backoff(&polls,0x100,0x100));
        assert(nv2a_idle_backoff(&polls,0x100,0x100));
    }
    for(unsigned prefix=0;prefix<16;prefix++) {
        polls=0;
        for(unsigned i=0;i<prefix;i++)assert(!nv2a_idle_backoff(&polls,0,0));
        /* Pending work, including ring wrap, cancels a partial idle run. */
        assert(!nv2a_idle_backoff(&polls,0xfffffffcu,0));
        for(unsigned i=0;i<15;i++)assert(!nv2a_idle_backoff(&polls,0,0));
        assert(nv2a_idle_backoff(&polls,0,0));
    }
    for(unsigned i=0;i<10000;i++)assert(!nv2a_idle_backoff(&polls,4,8));
    return 0;
}
