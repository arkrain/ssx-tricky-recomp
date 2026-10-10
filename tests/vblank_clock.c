#include "vblank_clock.h"
#include <assert.h>
int main(void)
{
    XboxVblankClock clock={0};unsigned count=0;
    for(uint64_t now=0;now<60000;now++)count+=xbox_vblank_due(&clock,now);
    assert(count==3600);
    /* Irregular wakeups retain phase rather than drifting at each wake. */
    clock=(XboxVblankClock){0};count=0;uint64_t now=0;
    for(unsigned i=0;now<60000;i++) {
        count+=xbox_vblank_due(&clock,now);
        now+=i%4+1;
    }
    assert(count==3600);
    clock=(XboxVblankClock){0};assert(xbox_vblank_due(&clock,0));
    assert(!xbox_vblank_due(&clock,16));assert(xbox_vblank_due(&clock,17));
    assert(!xbox_vblank_due(&clock,17));assert(!xbox_vblank_due(&clock,33));
    assert(xbox_vblank_due(&clock,34));assert(xbox_vblank_due(&clock,50));
    assert(xbox_vblank_due(&clock,100000));assert(!xbox_vblank_due(&clock,100000));
    assert(!xbox_vblank_due(&clock,100016));assert(xbox_vblank_due(&clock,100017));
    /* Long host uptime must not truncate to a signed/32-bit millisecond clock. */
    clock=(XboxVblankClock){0};count=0;
    uint64_t origin=UINT64_C(1)<<40;
    for(uint64_t i=0;i<60000;i++)count+=xbox_vblank_due(&clock,origin+i);
    assert(count==3600);
    return 0;
}
