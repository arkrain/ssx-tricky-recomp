#include "kernel.h"
#include "xbox_memory_layout.h"
#include <assert.h>
typedef void (*recomp_func_t)(void);
recomp_func_t recomp_lookup(uint32_t va){(void)va;return NULL;}
extern recomp_func_t recomp_lookup_kernel(uint32_t);
extern RECOMP_TLS uint32_t g_eax,g_esp;
extern ptrdiff_t g_xbox_mem_offset;
extern void xbox_kernel_set_thunk_address(uint32_t,uint32_t);
int main(void)
{
    uint32_t *memory=VirtualAlloc(NULL,64u*1024*1024,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    assert(memory);g_xbox_mem_offset=(ptrdiff_t)(uintptr_t)memory;
    memory[0x10000/4]=0x80000000u|301;
    xbox_kernel_set_thunk_address(0x10000,1);xbox_kernel_bridge_init();
    const uint32_t cases[][2]={
        {0,0},{0xc000009d,1167},{0xc000007f,112},{0xc0000034,2},
        {0xc000003a,3},{0xc0000022,5},{0xc0000008,6},{0xc0000017,8},
        {0xc000000d,87},{0xc0000035,183},{0xc0000002,120},{0xc0000011,38},
        {0xc000009a,1450},{0x12345678,317}
    };
    for(unsigned i=0;i<sizeof cases/sizeof cases[0];i++) {
        assert(xbox_RtlNtStatusToDosError((NTSTATUS)cases[i][0])==cases[i][1]);
        g_esp=0x20000;memory[g_esp/4]=0x12345678;memory[g_esp/4+1]=cases[i][0];
        recomp_lookup_kernel(memory[0x10000/4])();assert(g_eax==cases[i][1]);
    }
    VirtualFree(memory,0,MEM_RELEASE);return 0;
}
