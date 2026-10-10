/* Startup cinematics use native playback, with an explicit bring-up skip. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int (*movie_player)(const char *);
void ssx_register_startup_movie_player(int (*play)(const char *)) { movie_player=play; }

int ssx_skip_startup_movie(volatile uint8_t *player, const char *filename)
{
    const char *enabled = getenv("SSX_SKIP_INTRO");
    if (strcmp(filename, "data/video/eabig.mpc") != 0 &&
        strcmp(filename, "data/video/ssxintro.mpc") != 0) return 0;
    int skipped=enabled && strcmp(enabled,"1")==0;
    if(!skipped && (!movie_player || movie_player(filename)<=0))return 0;
    /* 0x00148D20 constructs the player and saves the previous render target.
     * Its destructor restores that target. Before 0x00148E00 allocates stream
     * resources, leave the player complete (+0x97), with end-of-stream (+0x96) set and input/abort flags
     * clear. Original queries and screen teardown then advance the queue. */
    player[0x94] = 0;
    player[0x95] = 0;
    player[0x96] = 1;
    player[0x97] = 1;
    if(skipped)fprintf(stderr, "[SSX] Intro skip enabled: %s\n", filename);
    return 1;
}
