#include "recomp_types.h"
#include <assert.h>
#include <stdlib.h>
ptrdiff_t g_xbox_mem_offset;
int main(void)
{
    unsigned char *regs = calloc(1, 0x601000);
    assert(regs);
    g_xbox_mem_offset = (ptrdiff_t)(uintptr_t)regs - 0xFD000000ull;
    RECOMP_STORE32(0xFD000400u, 0x12345678u);
    assert(MEM32(0xFD000400u) == 0x12345678u);
    MEM32(0xFD600100u) = 1;
    RECOMP_STORE32(0xFD600140u, 1);
    assert(MEM32(0xFD000100u) == (1u << 24));
    RECOMP_STORE32(0xFD600100u, 0);
    assert(MEM32(0xFD600100u) == 1);
    RECOMP_STORE32(0xFD600100u, 2);
    assert(MEM32(0xFD600100u) == 1);
    MEM32(0xFD000100u) |= 0x1000;
    RECOMP_STORE32(0xFD600100u, 1);
    assert(MEM32(0xFD600100u) == 0);
    assert(MEM32(0xFD000100u) == 0x1000);
    MEM32(0xFD600100u) = 1;
    RECOMP_STORE32(0xFD600140u, 0);
    assert(MEM32(0xFD600100u) == 1);
    assert(MEM32(0xFD000100u) == 0x1000);
    RECOMP_STORE32(0xFD600140u, 1);
    assert(MEM32(0xFD000100u) == 0x1001000);
    MEM32(0xFD400100u) = 1u << 20;
    RECOMP_STORE32(0xFD400140u, 1u << 20);
    RECOMP_STORE32(0xFD400100u, 1);
    assert(MEM32(0xFD400100u) == (1u << 20));
    RECOMP_STORE32(0xFD400100u, 1u << 20);
    assert(MEM32(0xFD400100u) == 0);
    assert(MEM32(0xFD000100u) == (1u << 24));
    /* A memory-cache flush must complete without the timer/ack thread. */
    MEM32(0xFD100410u)=0x10000u;
    assert(RECOMP_READ32(0xFD100410u)==0);
    for(unsigned i=0;i<10000;i++) {
        RECOMP_STORE32(0xFD100410u,0x12350000u);
        assert(!(MEM32(0xFD100410u)&0x10000u));
        assert(RECOMP_READ32(0xFD100410u)==0);
    }
    RECOMP_STORE32(0xFD100414u,0x10000u);
    assert(RECOMP_READ32(0xFD100414u)==0x10000u);
    free(regs);
    return 0;
}
