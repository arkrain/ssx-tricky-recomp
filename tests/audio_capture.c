/* The diagnostic trigger must preserve output, remain bounded and rearm. */
#include "apu_state.h"
#include "apu_xaudio2.h"
#include <assert.h>
#include <sys/stat.h>
#include <unistd.h>
#include <sys/wait.h>
int xa2_init(void){return 1;}
void xa2_shutdown(void){}
int xa2_is_active(void){return 1;}
int xa2_get_buffer_size(void){return 1024;}
static int16_t expected_left,expected_right;
static unsigned submissions;
int xa2_submit_samples(const int16_t *p,int n){
    assert(n==256);
    for(int i=0;i<n;i++){assert(p[i*2]==expected_left);assert(p[i*2+1]==expected_right);}
    submissions++;return 1;
}
void mcpx_apu_monitor_frame(MCPXAPUState *d);
static void frames(MCPXAPUState *d,unsigned count,int16_t left,int16_t right) {
    unsigned before=submissions;expected_left=left;expected_right=right;
    for(unsigned n=0;n<count;n++) {
        d->ep_frame_div=7;
        for(unsigned i=0;i<256;i++){d->monitor.frame_buf[i][0]=left;d->monitor.frame_buf[i][1]=right;}
        mcpx_apu_monitor_frame(d);
    }
    assert(submissions-before==count);
}
static void request(const char *path){FILE *f=fopen(path,"wb");assert(f);assert(!fclose(f));}
static void check(const char *path,int16_t left,int16_t right,unsigned samples) {
    struct stat st;assert(!stat(path,&st));assert(st.st_size==(off_t)samples*4);
    FILE *f=fopen(path,"rb");assert(f);
    for(unsigned i=0;i<samples;i++) {int16_t p[2];assert(fread(p,4,1,f)==1);assert(p[0]==left && p[1]==right);}
    assert(fgetc(f)==EOF);fclose(f);
}
static void run(const char *setting,unsigned seconds) {
    if(setting)setenv("RECOMP_AUDIO_SECONDS",setting,1);else unsetenv("RECOMP_AUDIO_SECONDS");
    unsigned samples=seconds*48000,blocks=samples/256+100;
    char dir[]="/tmp/ssx-audio-capture-XXXXXX";assert(mkdtemp(dir));
    char output[256],trigger[256];snprintf(output,sizeof output,"%s/output.pcm",dir);snprintf(trigger,sizeof trigger,"%s/request",dir);
    setenv("RECOMP_AUDIO_DUMP",output,1);setenv("RECOMP_AUDIO_TRIGGER",trigger,1);
    MCPXAPUState *d=calloc(1,sizeof *d);assert(d);
    frames(d,100,123,-456);assert(access(output,F_OK));
    request(trigger);frames(d,100,0,0);assert(access(trigger,F_OK));
    frames(d,blocks,123,-456);check(output,123,-456,samples);
    frames(d,100,789,-111);check(output,123,-456,samples);
    request(trigger);frames(d,blocks,789,-111);check(output,789,-111,samples);
    free(d);assert(!remove(output));assert(!rmdir(dir));
}

int main(void) {
    const struct {const char *setting;unsigned seconds;} cases[]={
        {NULL,10},{"1",1},{"2",2},{"60",60},{"0",10},{"61",10},
        {"-1",10},{"5oops",10},{"999999999999999999999999999999",10}};
    for(unsigned i=0;i<sizeof cases/sizeof cases[0];i++) {
        pid_t child=fork();assert(child>=0);
        if(!child){run(cases[i].setting,cases[i].seconds);_exit(0);}
        int status;assert(waitpid(child,&status,0)==child);
        assert(WIFEXITED(status) && WEXITSTATUS(status)==0);
    }
    return 0;
}

int xa2_get_queued_frames(void) { return -1; }
int xa2_get_target_frames(void) { return -1; }
