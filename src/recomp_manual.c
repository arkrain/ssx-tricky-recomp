#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "xbox_memory_layout.h"
extern RECOMP_TLS uint32_t g_eax, g_esp;
extern RECOMP_TLS int g_df;
extern ptrdiff_t g_xbox_mem_offset;
void sub_0015DB00(void);
typedef void (*recomp_func_t)(void);
recomp_func_t recomp_lookup_manual(uint32_t va) { return va == 0x0015DB00 ? sub_0015DB00 : NULL; }
static void print_guest_call_context(void)
{
    fprintf(stderr, "[SSX] eax=0x%08X esp=0x%08X", g_eax, g_esp);
    if (g_xbox_mem_offset && g_esp >= 0x1000 && g_esp <= g_xbox_total_ram - 4) {
        uint32_t ret;
        memcpy(&ret, (void *)((uintptr_t)g_xbox_mem_offset + g_esp), sizeof(ret));
        fprintf(stderr, " guest return=0x%08X", ret);
    }
    fputc('\n', stderr);
}
/* Fail visibly instead of silently skipping unknown calls during bring-up. */
void recomp_icall_fail_log(uint32_t va)
{
    fprintf(stderr, "[SSX] Unresolved indirect call: 0x%08X\n", va);
    print_guest_call_context();
    abort();
}
void recomp_direct_call_missing(uint32_t va)
{
    fprintf(stderr, "[SSX] Missing direct-call body: 0x%08X\n", va);
    print_guest_call_context();
    abort();
}
void recomp_icall_not_code_log(uint32_t va)
{
    fprintf(stderr, "[SSX] Indirect call to non-code: 0x%08X\n", va);
    print_guest_call_context();
    abort();
}

/* Verified against 0x0015DB00: cdecl memmove(dst, src, size). The original
 * uses inline jump tables that the linear disassembler decodes as code.
 * Preserve nonvolatile registers and pop only the guest return address. */
void sub_0015DB00(void)
{
    uint32_t args[3];
    memcpy(args, (void *)((uintptr_t)g_xbox_mem_offset + g_esp + 4), sizeof(args));
    if (args[2])
        memmove((void *)((uintptr_t)g_xbox_mem_offset + args[0]),
                (void *)((uintptr_t)g_xbox_mem_offset + args[1]), args[2]);
    g_eax = args[0];
    g_df = 0;
    g_esp += 4;
}
