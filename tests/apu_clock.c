#include "apu_shim.h"
#include <assert.h>
static int64_t ticks, frequency;
BOOL QueryPerformanceCounter(PLARGE_INTEGER value) { value->QuadPart=ticks; return TRUE; }
BOOL QueryPerformanceFrequency(PLARGE_INTEGER value) { value->QuadPart=frequency; return TRUE; }
int main(void) {
    const int64_t rates[]={1000000000,10000000};
    for(int r=0;r<2;r++) {
        frequency=rates[r];
        for(int days=0;days<=365;days++) {
            int64_t seconds=(int64_t)days*86400+12345;
            ticks=seconds*frequency+frequency/4;
            assert(qemu_clock_get_ns(0)==seconds*1000000000+250000000);
            assert(qemu_clock_get_us(0)==seconds*1000000+250000);
        }
    }
    return 0;
}
