#include "apu_shim.h"
#include <assert.h>
uint8_t *g_apu_ram_ptr, *g_apu_low_ram_ptr;
size_t g_apu_low_ram_size;
int main(void) {
    uint32_t physical[64]={0}, ordinary[64]={0};
    g_apu_ram_ptr=(uint8_t *)physical;g_apu_low_ram_ptr=(uint8_t *)ordinary;
    g_apu_low_ram_size=sizeof(ordinary);
    physical[20]=0xdeadbeef;ordinary[20]=0x12345678;
    assert(ldl_le_phys(NULL,80)==0xdeadbeef);
    assert(ldl_le_phys(NULL,0x40000050)==0x12345678);
    stl_le_phys(NULL,0x40000050,0xaabbccdd);assert(ordinary[20]==0xaabbccdd);
    assert(physical[20]==0xdeadbeef);
    stw_le_phys(NULL,0x40000052,0x1122);assert(ordinary[20]==0x1122ccdd);
    stb_phys(NULL,0x40000050,0);assert(ldub_phys(NULL,0x40000050)==0);
    assert(lduw_le_phys(NULL,0x40000052)==0x1122);
    assert(apu_phys_ptr(0x40000050,16)==(uint8_t *)&ordinary[20]);
    return 0;
}
