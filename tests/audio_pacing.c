/* Device periods must not turn a ten-ms refill interval into 1024 consumed
 * samples. Exercise the real throttle with a deterministic 512-frame device. */
#include <stdint.h>
static int64_t fake_us;
static int64_t test_clock(int type){(void)type;return fake_us;}
#define qemu_clock_get_us test_clock
#define qemu_cond_timedwait test_wait
#include "apu_state.h"
static int queued=2048;
static int64_t next_consume;
static void consume(void){while(fake_us*3>=next_consume){queued-=512;next_consume+=32000;}}
static void test_wait(QemuCond *c,QemuMutex *m,int ms){(void)c;(void)m;fake_us+=ms*1000;consume();}
#include "../third_party/xboxrecomp/src/apu/apu_core.c"
#include <assert.h>
int xa2_init(void){return 1;}
void xa2_shutdown(void){}
int xa2_is_active(void){return 1;}
int xa2_get_buffer_size(void){return 1024;}
int xa2_get_target_frames(void){return 2048;}
int xa2_get_queued_frames(void){consume();return queued;}
int xa2_submit_samples(const int16_t *p,int n){(void)p;queued+=n;return 1;}
int main(void)
{
    for(int phase=1;phase<=32;phase++) {
        MCPXAPUState d={0};fake_us=0;queued=2048;next_consume=phase*1000;
        int64_t events[1000];int oldest=0;
        for(int i=0;i<1000;i++) {
            throttle(&d);events[i]=fake_us;queued+=256;
            while(events[i]-events[oldest]>13000)oldest++;
            if((i-oldest+1)*256>768)fprintf(stderr,"phase=%d i=%d now=%lld oldest=%lld queued=%d\n",phase,i,(long long)fake_us,(long long)events[oldest],queued);
            assert((i-oldest+1)*256<=768);
            assert(queued>=1024 && queued<=2560);
        }
        assert(fake_us>5200000 && fake_us<5500000);
    }
    /* A25-ms source-publication delay drains the device queue. Refill its
     * safety lead before resuming the ordinary smooth deadline. */
    MCPXAPUState recovery={0};fake_us=0;queued=512;next_consume=32000;
    for(int i=0;i<4;i++){throttle(&recovery);queued+=256;assert(fake_us==0);}
    assert(queued==1536);
    puts("512-frame device periods preserve software refill lead across32 phase offsets");
    return 0;
}
