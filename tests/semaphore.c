#include "nv2a_semaphore.h"
#include <stdio.h>
#define CHECK(x) do { if (!(x)) { fprintf(stderr, "failed: %s\n", #x); return 1; } } while (0)
static void put(uint8_t *p, uint32_t v) { memcpy(p, &v, 4); }
int main(void)
{
    uint8_t ramin[8192] = {0}, physical[16384] = {0};
    /* Actual SSX initialization DMA descriptor, captured from RAMIN. */
    put(ramin + 8*8, 8); put(ramin + 8*8 + 4, 0x8000011b);
    put(ramin + 0x11b0, 0x0002b03d);
    put(ramin + 0x11b4, 0x20); put(ramin + 0x11b8, 0x2003);
    CHECK(nv2a_semaphore_write(ramin, sizeof(ramin), 0x03000000, 8, 0, 5, physical, sizeof(physical)));
    CHECK(sem_u32(physical + 0x2000) == 5);
    CHECK(nv2a_semaphore_write(ramin, sizeof(ramin), 0x03000000, 8, 4, 7, physical, sizeof(physical)));
    CHECK(sem_u32(physical + 0x2004) == 7);
    CHECK(!nv2a_semaphore_write(ramin, sizeof(ramin), 0, 9, 0, 99, physical, sizeof(physical)));
    CHECK(!nv2a_semaphore_write(ramin, sizeof(ramin), 0, 8, 1, 99, physical, sizeof(physical)));
    CHECK(!nv2a_semaphore_write(ramin, sizeof(ramin), 0, 8, 0x20, 99, physical, sizeof(physical)));
    CHECK(!nv2a_semaphore_write(ramin, sizeof(ramin), 0x1f0, 8, 0, 99, physical, sizeof(physical)));
    CHECK(!nv2a_semaphore_write(ramin, sizeof(ramin), 0, 8, 0, 99, physical, 0x2002));
    /* Full report span must fit both the inclusive DMA limit and backing RAM. */
    CHECK(nv2a_dma_pointer(ramin,sizeof(ramin),0,8,0x10,16,physical,sizeof(physical))==physical+0x2010);
    CHECK(!nv2a_dma_pointer(ramin,sizeof(ramin),0,8,0x14,16,physical,sizeof(physical)));
    CHECK(!nv2a_dma_pointer(ramin,sizeof(ramin),0,8,0x10,16,physical,0x201f));
    CHECK(!nv2a_dma_pointer(ramin,sizeof(ramin),0,8,0,0,physical,sizeof(physical)));
    put(ramin + 8*8 + 4, 0x8100011b);
    CHECK(!nv2a_semaphore_write(ramin, sizeof(ramin), 0, 8, 0, 99, physical, sizeof(physical)));
    CHECK(sem_u32(physical + 0x2000) == 5 && sem_u32(physical + 0x2004) == 7);
    return 0;
}
