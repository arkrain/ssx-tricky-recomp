#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
int ssx_skip_startup_movie(volatile uint8_t *, const char *);
void ssx_register_startup_movie_player(int (*)(const char *));
static int callback_result,callback_calls;
static int play(const char *name){assert(!strcmp(name,"data/video/eabig.mpc"));callback_calls++;return callback_result;}
int main(void)
{
    uint8_t player[0xac], original[0xac];
    memset(player, 0xa5, sizeof(player)); memcpy(original, player, sizeof(player));
    unsetenv("SSX_SKIP_INTRO");
    assert(!ssx_skip_startup_movie(player, "data/video/eabig.mpc"));
    assert(!memcmp(player, original, sizeof(player)));
    setenv("SSX_SKIP_INTRO", "1", 1);
    assert(!ssx_skip_startup_movie(player, "data/video/tricks.mpc"));
    assert(!memcmp(player, original, sizeof(player)));
    assert(ssx_skip_startup_movie(player, "data/video/eabig.mpc"));
    assert(player[0x94]==0 && player[0x95]==0 && player[0x96]==1 && player[0x97]==1);
    assert(!memcmp(player, original, 0x94));
    assert(!memcmp(player+0x98, original+0x98, sizeof(player)-0x98));
    assert(ssx_skip_startup_movie(player, "data/video/ssxintro.mpc"));
    unsetenv("SSX_SKIP_INTRO");memcpy(player,original,sizeof(player));
    ssx_register_startup_movie_player(play);callback_result=0;
    assert(!ssx_skip_startup_movie(player,"data/video/eabig.mpc"));
    assert(callback_calls==1 && !memcmp(player,original,sizeof(player)));
    callback_result=1;assert(ssx_skip_startup_movie(player,"data/video/eabig.mpc"));
    assert(callback_calls==2 && player[0x97]==1);
    assert(!ssx_skip_startup_movie(player,"data/video/tricks.mpc"));assert(callback_calls==2);
    ssx_register_startup_movie_player(NULL);
    return 0;
}
