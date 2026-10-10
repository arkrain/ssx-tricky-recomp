#include "contiguous_pages.h"
#include <assert.h>
int main(void)
{
    ContiguousPages arena = {0};
    const uint32_t capacity = 8 * 4096;
    assert(contiguous_take(&arena, capacity, 5000, 4096, 0, UINT32_MAX) == 0);
    assert(contiguous_take(&arena, capacity, 256, 8192, 0, UINT32_MAX) == 8192);
    assert(!contiguous_release(&arena, 4096)); /* interior pointer */
    assert(contiguous_take(&arena, capacity, 4096, 4096, 12288, 16383) == 12288);
    assert(contiguous_take(&arena, capacity, 4096, 4096, 12288, 16383) == CONTIG_NO_SPACE);
    assert(contiguous_release(&arena, 0));
    assert(!contiguous_release(&arena, 0));
    assert(contiguous_take(&arena, capacity, 8192, 4096, 0, 8191) == 0);
    assert(contiguous_release(&arena, 0));
    assert(contiguous_release(&arena, 8192));
    assert(contiguous_release(&arena, 12288));
    /* More allocations than the old bump arena could ever service. */
    for (unsigned i = 0; i < 20000; ++i) {
        uint32_t p = contiguous_take(&arena, capacity, 256, 4096, 0, UINT32_MAX);
        assert(p == 0);
        assert(contiguous_release(&arena, p));
    }
    assert(contiguous_take(&arena, capacity, capacity, 4096, 0, UINT32_MAX) == 0);
    assert(contiguous_take(&arena, capacity, 1, 4096, 0, UINT32_MAX) == CONTIG_NO_SPACE);
    assert(contiguous_release(&arena, 0));
    assert(contiguous_take(&arena, capacity, 1, 6000, 0, UINT32_MAX) == CONTIG_NO_SPACE);
    assert(contiguous_take(&arena, capacity, UINT32_MAX, 4096, 0, UINT32_MAX) == CONTIG_NO_SPACE);
    assert(contiguous_take(&arena, capacity, 4096, 4096, UINT32_MAX, UINT32_MAX) == CONTIG_NO_SPACE);
    /* A large game heap must leave low pages for later 24-bit DMA buffers. */
    memset(&arena,0,sizeof(arena));
    uint32_t full=64u*1024*1024;
    uint32_t framebuffer=contiguous_take_high(&arena,full,5u*1024*1024,4096,0,UINT32_MAX);
    uint32_t heap=contiguous_take_high(&arena,full,53u*1024*1024,4096,0,UINT32_MAX);
    assert(framebuffer==59u*1024*1024 && heap==6u*1024*1024);
    assert(contiguous_take(&arena,full,4096,4096,0,0xffffff)==0);
    assert(contiguous_take_high(&arena,full,8192,8192,0,UINT32_MAX)==6u*1024*1024-8192);
    assert(contiguous_release(&arena,heap));
    assert(contiguous_take_high(&arena,full,53u*1024*1024,4096,0,UINT32_MAX)==heap);
    assert(contiguous_take_high(&arena,full,4096,6000,0,UINT32_MAX)==CONTIG_NO_SPACE);
    assert(contiguous_take_high(&arena,full,4096,4096,full,UINT32_MAX)==CONTIG_NO_SPACE);
    return 0;
}
