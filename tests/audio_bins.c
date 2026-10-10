#include "apu_state.h"
#include <assert.h>
#include <sys/stat.h>
#include <unistd.h>
void mcpx_apu_dsp_frame(MCPXAPUState *,float [NUM_MIXBINS][NUM_SAMPLES_PER_FRAME]);
static void captured_frame(MCPXAPUState *d,float bins[32][32]) {
    mcpx_apu_capture_mixbins(bins); /* Common VP→DSP boundary. */
    mcpx_apu_dsp_frame(d,bins);
}
static void request(const char *path){FILE *f=fopen(path,"wb");assert(f);assert(!fclose(f));}
static void check(const char *path,const float bins[32][32]) {
    struct stat st;assert(!stat(path,&st));assert(st.st_size==15000*32*32*4);
    FILE *f=fopen(path,"rb");assert(f);float block[32][32];
    assert(fread(block,sizeof block,1,f)==1);assert(!memcmp(block,bins,sizeof block));
    assert(!fseek(f,-(long)sizeof block,SEEK_END));
    assert(fread(block,sizeof block,1,f)==1);assert(!memcmp(block,bins,sizeof block));
    assert(fgetc(f)==EOF);fclose(f);
}
int main(void) {
    setenv("RECOMP_APU_STEREO_DOWNMIX","0",1);
    char dir[]="/tmp/ssx-audio-bins-XXXXXX";assert(mkdtemp(dir));
    char output[256],trigger[256];snprintf(output,sizeof output,"%s/output.bin",dir);snprintf(trigger,sizeof trigger,"%s/request",dir);
    setenv("RECOMP_APU_BINS_DUMP",output,1);setenv("RECOMP_APU_BINS_TRIGGER",trigger,1);
    MCPXAPUState *d=calloc(1,sizeof *d);assert(d);float bins[32][32],original[32][32];
    for(unsigned b=0;b<32;b++)for(unsigned i=0;i<32;i++)bins[b][i]=(b*32+i+1)*.00001f;
    memcpy(original,bins,sizeof bins);
    for(unsigned n=0;n<200;n++)captured_frame(d,bins);
    assert(access(output,F_OK));
    request(trigger);
    for(unsigned n=0;n<15200;n++)captured_frame(d,bins);
    assert(access(trigger,F_OK));check(output,bins);assert(!memcmp(original,bins,sizeof bins));
    for(unsigned i=0;i<32;i++) {
        assert(d->monitor.frame_buf[i][0]==(int16_t)(bins[0][i]*32767.f));
        assert(d->monitor.frame_buf[i][1]==(int16_t)(bins[1][i]*32767.f));
    }
    bins[31][31]=.125f;
    for(unsigned n=0;n<200;n++)captured_frame(d,bins);
    check(output,original);
    request(trigger);for(unsigned n=0;n<15200;n++)captured_frame(d,bins);check(output,bins);
    free(d);assert(!remove(output));assert(!rmdir(dir));return 0;
}
