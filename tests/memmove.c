#include "xbox_memory_layout.h"
#include <stdio.h>
#include <stdint.h>
#include <string.h>
RECOMP_TLS uint32_t g_eax, g_esp;
RECOMP_TLS int g_df;
ptrdiff_t g_xbox_mem_offset;
size_t g_xbox_total_ram = 1024;
void sub_0015DB00(void);
int main(void)
{
    unsigned char memory[1024], expected[1024], snapshot[128];
    const uint32_t destinations[] = {128, 129, 127, 256};
    g_xbox_mem_offset = (ptrdiff_t)(uintptr_t)memory;
    for (unsigned d = 0; d < 4; ++d) {
        for (uint32_t n = 0; n <= 96; ++n) {
            for (unsigned i = 0; i < sizeof(memory); ++i) memory[i] = i * 37;
            uint32_t stack[] = {0x12345678, destinations[d], 128, n};
            memcpy(memory + 800, stack, sizeof(stack));
            memcpy(expected, memory, sizeof(memory));
            memcpy(snapshot, memory + 128, n);
            for (uint32_t i = 0; i < n; ++i) expected[destinations[d] + i] = snapshot[i];
            g_esp = 800;
            g_df = 0;
            sub_0015DB00();
            if (memcmp(memory, expected, sizeof(memory)) || g_eax != destinations[d] || g_esp != 804 || g_df != 0) {
                fprintf(stderr, "memmove mismatch: dst=%u len=%u\n", destinations[d], n);
                return 1;
            }
        }
    }
    return 0;
}
