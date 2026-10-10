#include "kernel.h"
#include "xbox_memory_layout.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
typedef void (*recomp_func_t)(void);
recomp_func_t recomp_lookup(uint32_t va) { (void)va; return NULL; }
extern recomp_func_t recomp_lookup_kernel(uint32_t);
extern RECOMP_TLS uint32_t g_eax, g_esp;
extern ptrdiff_t g_xbox_mem_offset;
extern void xbox_kernel_set_thunk_address(uint32_t, uint32_t);
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "failed: %s\n", #x); return 1; } } while (0)
static uint32_t *memory;
static uint32_t call(unsigned slot, const uint32_t *args, size_t n)
{
    g_esp = 0x20000;
    memory[g_esp / 4] = 0x12345678;
    memcpy((char*)memory + g_esp + 4, args, n * 4);
    recomp_lookup_kernel(memory[0x10000/4 + slot])();
    return g_eax;
}
int main(void)
{
    memory = VirtualAlloc(NULL, 64u*1024*1024, MEM_RESERVE|MEM_COMMIT, PAGE_READWRITE);
    CHECK(memory != NULL);
    g_xbox_mem_offset = (ptrdiff_t)(uintptr_t)memory;
    memory[0x10000/4] = 0x80000000u | 159;
    memory[0x10004/4] = 0x80000000u | 145;
    memory[0x10008/4] = 0x80000000u | 158;
    xbox_kernel_set_thunk_address(0x10000, 3);
    xbox_kernel_bridge_init();
    const uint32_t obj = 0x30000;
    uint32_t *event = memory + obj/4;
    event[0] = 0x00040000; event[1] = 1;
    event[2] = event[3] = obj + 8;
    uint32_t wait[] = {obj, 6, 1, 0, 0x40000}; /* zero timeout */
    uint32_t set[] = {obj, 0, 0};
    CHECK(call(0, wait, 5) == 0);
    CHECK(call(0, wait, 5) == 0); /* notification event remains signalled */
    event[1] = 0; /* direct XDK reset */
    CHECK(call(0, wait, 5) == 0x102);
    CHECK(call(1, set, 3) == 0);
    CHECK(event[1] == 1 && call(0, wait, 5) == 0);
    /* Another inline object, auto-reset this time. */
    wait[0] = obj + 32; event += 8;
    event[0] = 0x00040001; event[1] = 1;
    event[2] = event[3] = wait[0] + 8;
    CHECK(call(0, wait, 5) == 0 && event[1] == 0);
    CHECK(call(0, wait, 5) == 0x102);
    wait[0] = 0x1234;
    CHECK(call(0, wait, 5) == 0xc0000008u);
    CHECK(g_esp == 0x20000 + 24);
    memory[0x50000/4] = obj + 32;
    memory[0x50004/4] = obj;
    uint32_t multiple[] = {2, 0x50000, 1, 6, 1, 0, 0x40000, 0};
    CHECK(call(2, multiple, 8) == 1); /* first unsignalled, second signalled */
    event[1] = 1;
    CHECK(call(2, multiple, 8) == 0 && event[1] == 0);
    multiple[2] = 0;
    CHECK(call(2, multiple, 8) == 0x102);
    event[1] = 1;
    CHECK(call(2, multiple, 8) == 0 && event[1] == 0);
    multiple[0] = 65;
    CHECK(call(2, multiple, 8) == 0xc000000du);
    return 0;
}
