#include <assert.h>
#include <stdint.h>
#include "video/fb_timing.h"

int main(void)
{
    const uint64_t frequency=1000000000;
    uint64_t deadline=0;
    /* Three milliseconds of work leaves fourteen milliseconds of rounded
     * sleep, rather than imposing another sixteen milliseconds. */
    assert(fb_frame_wait_ms(&deadline,3000000,frequency)==14);
    assert(deadline==16666666);
    /* The next deadline absorbs the previous sleep's sub-millisecond excess. */
    assert(fb_frame_wait_ms(&deadline,20000000,frequency)==14);
    assert(deadline==33333332);
    assert(fb_frame_wait_ms(&deadline,70000000,frequency)==0);
    assert(deadline==70000000);
    assert(fb_frame_wait_ms(&deadline,73000000,frequency)==14);
    /* Simulate a minute with variable work and millisecond-only sleeps.
     * Rounding must not accumulate into an extra sleep interval per frame. */
    uint64_t now=0;deadline=0;
    for(unsigned frame=0;frame<3600;frame++) {
        now+=(2+frame%9)*1000000;
        now+=(uint64_t)fb_frame_wait_ms(&deadline,now,frequency)*1000000;
    }
    assert(now>=59999000000ULL && now<=60001000000ULL);
    return 0;
}
