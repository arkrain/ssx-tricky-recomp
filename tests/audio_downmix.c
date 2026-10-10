#include "apu_state.h"
#include <assert.h>
void mcpx_apu_dsp_frame(MCPXAPUState *,float [NUM_MIXBINS][NUM_SAMPLES_PER_FRAME]);
int main(int argc,char **argv) {
    int legacy=argc>1;setenv("RECOMP_APU_STEREO_DOWNMIX",legacy?"0":"1",1);
    unsetenv("RECOMP_APU_BINS_DUMP");unsetenv("RECOMP_APU_BINS_TRIGGER");
    MCPXAPUState *d=calloc(1,sizeof *d);assert(d);
    /* One isolated speaker at a time: center goes to both sides; rear
     * speakers retain their side. LFE and non-speaker sends do not leak. */
    const int expected[7][2]={{8191,0},{0,8191},{5792,5792},{0,0},{5792,0},{0,5792},{0,0}};
    for(int bin=0;bin<7;bin++) {
        float input[32][32]={0},saved[32][32];
        for(int i=0;i<32;i++)input[bin][i]=.25f;
        memcpy(saved,input,sizeof input);d->ep_frame_div=bin;
        mcpx_apu_dsp_frame(d,input);assert(!memcmp(input,saved,sizeof input));
        for(int i=0;i<32;i++)for(int ch=0;ch<2;ch++) {
            int want=legacy && bin>=2?0:expected[bin][ch];
            assert(abs(d->monitor.frame_buf[bin*32+i][ch]-want)<=1);
        }
    }
    float input[32][32]={0};d->ep_frame_div=0;
    for(int i=0;i<32;i++) {
        input[0][i]=.8f;input[1][i]=-.8f;
        input[4][i]=.8f;input[5][i]=-.8f;
    }
    mcpx_apu_dsp_frame(d,input);
    for(int i=0;i<32;i++) {
        int want=legacy?(int16_t)(.8f*32767.f):32767;
        assert(d->monitor.frame_buf[i][0]==want && d->monitor.frame_buf[i][1]==-want);
    }
    free(d);return 0;
}
