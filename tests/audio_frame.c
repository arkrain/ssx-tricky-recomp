#include "apu_state.h"
#include "apu_xaudio2.h"
#include <assert.h>
static int calls, frames;
static int16_t captured[256][2];
int xa2_init(void) { return 1; }
void xa2_shutdown(void) {}
int xa2_is_active(void) { return 1; }
int xa2_get_buffer_size(void) { return 1024; }
int xa2_submit_samples(const int16_t *data, int count) {
    assert(count == 256); ++calls; frames += count;
    memcpy(captured, data, sizeof(captured)); return 1;
}
extern volatile int g_audio_muted;
void mcpx_apu_monitor_frame(MCPXAPUState *d);
int main(void) {
    MCPXAPUState *d = calloc(1, sizeof(*d)); assert(d);
    /* Eight VP/DSP subframes accumulate exactly one EP output frame. */
    for (int n=0;n<8;n++) {
        d->ep_frame_div=n;
        for(int i=0;i<32;i++) {
            d->monitor.frame_buf[n*32+i][0]=n*32+i+1;
            d->monitor.frame_buf[n*32+i][1]=-n*32-i-1;
        }
        mcpx_apu_monitor_frame(d);
        assert(calls==(n==7));
    }
    assert(frames==256);
    for(int i=0;i<256;i++) {
        assert(captured[i][0]==i+1 && captured[i][1]==-i-1);
        assert(!d->monitor.frame_buf[i][0] && !d->monitor.frame_buf[i][1]);
    }
    memset(d->monitor.frame_buf, 1, sizeof(d->monitor.frame_buf));
    g_audio_muted=1; d->ep_frame_div=15; mcpx_apu_monitor_frame(d);
    assert(calls==2 && frames==512);
    for(int i=0;i<256;i++) assert(!captured[i][0] && !captured[i][1]);
    free(d); return 0;
}

int xa2_get_queued_frames(void) { return -1; }
int xa2_get_target_frames(void) { return -1; }
