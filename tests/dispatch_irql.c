#include "kernel.h"
#include "xbox_memory_layout.h"
#include <assert.h>
typedef void (*recomp_func_t)(void);
recomp_func_t recomp_lookup(uint32_t va){(void)va;return NULL;}
extern recomp_func_t recomp_lookup_kernel(uint32_t);
extern RECOMP_TLS uint32_t g_eax,g_ecx,g_esp;
extern ptrdiff_t g_xbox_mem_offset;
extern void xbox_kernel_set_thunk_address(uint32_t,uint32_t);
static HANDLE attempting,entered,isr_done;
static volatile LONG inside,overlap,total;
static DWORD WINAPI dpc_worker(LPVOID unused)
{
    (void)unused;SetEvent(attempting);
    for(unsigned i=0;i<1000;i++) {
        KIRQL old=xbox_KeRaiseIrqlToDpcLevel();assert(old==0);
        assert(xbox_GetCurrentIrql()==DISPATCH_LEVEL);SetEvent(entered);
        if(InterlockedIncrement(&inside)!=1)InterlockedIncrement(&overlap);
        Sleep(0);InterlockedIncrement(&total);InterlockedDecrement(&inside);
        KIRQL nested=xbox_KfRaiseIrql(3);assert(nested==2);
        xbox_KfLowerIrql(nested);assert(xbox_GetCurrentIrql()==2);
        xbox_KfLowerIrql(old);assert(xbox_GetCurrentIrql()==0);
    }
    return 0;
}
static DWORD WINAPI isr_worker(LPVOID unused)
{
    (void)unused;KIRQL old=xbox_EnterIsr();assert(old==0);
    assert(xbox_GetCurrentIrql()>DISPATCH_LEVEL);
    KIRQL nested=xbox_KfRaiseIrql(5);xbox_KfLowerIrql(nested);
    xbox_LeaveIsr(old);assert(xbox_GetCurrentIrql()==0);
    SetEvent(isr_done);return 0;
}
int main(void)
{
    attempting=CreateEventW(NULL,TRUE,FALSE,NULL);entered=CreateEventW(NULL,TRUE,FALSE,NULL);
    isr_done=CreateEventW(NULL,TRUE,FALSE,NULL);
    assert(xbox_KeRaiseIrqlToDpcLevel()==0);
    HANDLE dpc=CreateThread(NULL,0,dpc_worker,NULL,0,NULL);
    assert(WaitForSingleObject(attempting,5000)==WAIT_OBJECT_0);
    assert(WaitForSingleObject(entered,30)==WAIT_TIMEOUT);
    HANDLE isr=CreateThread(NULL,0,isr_worker,NULL,0,NULL);
    assert(WaitForSingleObject(isr_done,5000)==WAIT_OBJECT_0);
    xbox_KfLowerIrql(0);
    for(unsigned i=0;i<1000;i++) {
        assert(xbox_KfRaiseIrql(3)==0);
        if(InterlockedIncrement(&inside)!=1)InterlockedIncrement(&overlap);
        Sleep(0);InterlockedIncrement(&total);InterlockedDecrement(&inside);
        xbox_KfLowerIrql(0);
    }
    assert(WaitForSingleObject(dpc,10000)==WAIT_OBJECT_0);
    assert(WaitForSingleObject(isr,10000)==WAIT_OBJECT_0);
    assert(!overlap && total==2000);
    uint32_t *ram=VirtualAlloc(NULL,64u*1024*1024,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    assert(ram);g_xbox_mem_offset=(ptrdiff_t)(uintptr_t)ram;
    ram[0x10000/4]=0x80000000u|129;ram[0x10004/4]=0x80000000u|103;
    ram[0x10008/4]=0x80000000u|161;
    xbox_kernel_set_thunk_address(0x10000,3);xbox_kernel_bridge_init();
    g_esp=0x20000;recomp_lookup_kernel(ram[0x10000/4])();assert(g_eax==0);
    g_esp=0x20000;recomp_lookup_kernel(ram[0x10004/4])();assert(g_eax==2);
    g_ecx=0;g_esp=0x20000;recomp_lookup_kernel(ram[0x10008/4])();
    assert(xbox_GetCurrentIrql()==0);
    VirtualFree(ram,0,MEM_RELEASE);CloseHandle(dpc);CloseHandle(isr);
    CloseHandle(attempting);CloseHandle(entered);CloseHandle(isr_done);return 0;
}
