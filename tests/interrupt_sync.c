#include "kernel.h"
#include "xbox_memory_layout.h"
#include <assert.h>
typedef void (*recomp_func_t)(void);
extern recomp_func_t recomp_lookup_kernel(uint32_t);
extern RECOMP_TLS uint32_t g_eax,g_esp;
extern ptrdiff_t g_xbox_mem_offset;
extern void xbox_kernel_set_thunk_address(uint32_t,uint32_t);
static uint32_t *ram;
static HANDLE held,release_isr,attempted,finished;
static volatile LONG inside,overlap,calls;
static const uint32_t interrupt_va=0x30000;
static void check_enter(void)
{
    if(InterlockedIncrement(&inside)!=1)InterlockedIncrement(&overlap);
    Sleep(0);
    InterlockedIncrement(&calls);
    InterlockedDecrement(&inside);
}
static void guest_callback(void)
{
    assert(xbox_GetCurrentIrql()>DISPATCH_LEVEL);
    assert(ram[g_esp/4+1]==0x12345678);
    check_enter();g_eax=0x5a;g_esp+=8;
}
recomp_func_t recomp_lookup(uint32_t va){return va==0x11000?guest_callback:NULL;}
static BOOLEAN __stdcall native_callback(PVOID p)
{
    assert(p==ram && xbox_GetCurrentIrql()>DISPATCH_LEVEL);check_enter();return 0x37;
}
static DWORD WINAPI isr_worker(LPVOID arg)
{
    (void)arg;PVOID object=(char*)ram+interrupt_va;
    xbox_InterruptEnter(object);xbox_InterruptEnter(object);
    SetEvent(held);assert(WaitForSingleObject(release_isr,5000)==WAIT_OBJECT_0);
    xbox_InterruptLeave(object);xbox_InterruptLeave(object);
    for(unsigned i=0;i<1000;i++) {
        xbox_InterruptEnter(object);check_enter();xbox_InterruptLeave(object);
    }
    return 0;
}
static DWORD WINAPI synchronize_worker(LPVOID arg)
{
    (void)arg;SetEvent(attempted);
    for(unsigned i=0;i<1000;i++) {
        g_esp=0x20000;ram[g_esp/4]=0;
        ram[g_esp/4+1]=interrupt_va;ram[g_esp/4+2]=0x11000;
        ram[g_esp/4+3]=0x12345678;
        recomp_lookup_kernel(ram[0x10000/4])();
        assert(g_eax==0x5a && g_esp==0x20010);
    }
    SetEvent(finished);return 0;
}
int main(void)
{
    ram=VirtualAlloc(NULL,64u*1024*1024,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);
    assert(ram);g_xbox_mem_offset=(ptrdiff_t)(uintptr_t)ram;
    ram[0x10000/4]=0x80000000u|153;
    xbox_kernel_set_thunk_address(0x10000,1);xbox_kernel_bridge_init();
    held=CreateEventW(NULL,TRUE,FALSE,NULL);release_isr=CreateEventW(NULL,TRUE,FALSE,NULL);
    attempted=CreateEventW(NULL,TRUE,FALSE,NULL);finished=CreateEventW(NULL,TRUE,FALSE,NULL);
    HANDLE isr=CreateThread(NULL,0,isr_worker,NULL,0,NULL);
    assert(WaitForSingleObject(held,5000)==WAIT_OBJECT_0);
    HANDLE sync=CreateThread(NULL,0,synchronize_worker,NULL,0,NULL);
    assert(WaitForSingleObject(attempted,5000)==WAIT_OBJECT_0);
    assert(WaitForSingleObject(finished,30)==WAIT_TIMEOUT && calls==0);
    SetEvent(release_isr);
    assert(WaitForSingleObject(isr,10000)==WAIT_OBJECT_0);
    assert(WaitForSingleObject(sync,10000)==WAIT_OBJECT_0);
    assert(!overlap && calls==2000);
    assert(xbox_KeSynchronizeExecution((PXBOX_KINTERRUPT)((char*)ram+interrupt_va),
        (PVOID)native_callback,ram)==0x37);
    assert(calls==2001 && !inside && xbox_GetCurrentIrql()==0);
    CloseHandle(isr);CloseHandle(sync);CloseHandle(held);CloseHandle(release_isr);
    CloseHandle(attempted);CloseHandle(finished);VirtualFree(ram,0,MEM_RELEASE);
    return 0;
}
