#include "ac97.h"
#include <assert.h>
#include <string.h>
int main(void) {
    uint8_t regs[128]; memset(regs,0xa5,sizeof(regs));
    regs[0x1b]=0x1d;
    assert(ssx_ac97_store8(regs,0x1b,2));
    assert(regs[0x1b]==0x1c && regs[0x16]==1);
    for(int i=0;i<11;i++)if(i!=6)assert(!regs[0x10+i]);
    assert(regs[0x0f]==0xa5 && regs[0x1c]==0xa5);
    assert(ssx_ac97_store8(regs,0x7b,2)); assert(!(regs[0x7b]&2));
    assert(!ssx_ac97_store8(regs,0x3b,2));
    assert(!ssx_ac97_store8(regs,0x15,2));
    assert(ssx_ac97_store8(regs,0x1b,0)); assert(regs[0x16]&1);
    return 0;
}
