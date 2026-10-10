/* Decode a local movie without guest execution; optional realtime audio/skip. */
#include "movie_player.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef struct { unsigned frames, skip_after;uint64_t hash;FILE *pcm; } Probe;
static void present(const uint32_t *pixels,void *opaque) {
    Probe *probe=opaque;probe->frames++;
    for(unsigned i=0;i<640*480;i++)probe->hash=(probe->hash^pixels[i])*1099511628211ULL;
}
static int skip(void *opaque){Probe *probe=opaque;return probe->skip_after && probe->frames>=probe->skip_after;}
static void audio_pcm(const int16_t *samples,unsigned frames,void *opaque) {
    Probe *probe=opaque;
    if(fwrite(samples,4,frames,probe->pcm)!=frames){perror("movie PCM write");exit(1);}
}
int main(int argc,char **argv) {
    if(argc<2){fprintf(stderr,"usage: movie_probe FILE [--audio | --skip-after N | --pcm PATH]\n");return 2;}
    Probe probe={0,0,1469598103934665603ULL};int audio=0;
    if(argc>2 && !strcmp(argv[2],"--audio"))audio=1;
    if(argc>3 && !strcmp(argv[2],"--skip-after"))probe.skip_after=(unsigned)strtoul(argv[3],NULL,10);
    if(argc>3 && !strcmp(argv[2],"--pcm")){probe.pcm=fopen(argv[3],"wb");if(!probe.pcm){perror(argv[3]);return 1;}}
    SsxMovieOutput output={present,skip,&probe,audio,audio,probe.pcm?audio_pcm:NULL};
    int result=ssx_movie_play(argv[1],&output);
    if(probe.pcm)fclose(probe.pcm);
    printf("frames=%u hash=%016llx result=%d\n",probe.frames,(unsigned long long)probe.hash,result);
    return result==(probe.skip_after?2:1)?0:1;
}
