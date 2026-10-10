#include "movie_player.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "xinput_xbox.h"
extern void xbox_FramebufferMovieFrame(const uint32_t *pixels);
extern void ssx_register_startup_movie_player(int (*play)(const char *));
static char game_directory[4096];
typedef struct { unsigned previous; } MovieInput;
static unsigned buttons(void) {
    XBOX_INPUT_STATE state={0};
    xbox_InputGetState(0,&state);
    return (state.Gamepad.wButtons&0x10) || state.Gamepad.bAnalogButtons[0]>30 || state.Gamepad.bAnalogButtons[1]>30;
}
static int skip(void *opaque) {
    MovieInput *input=opaque;unsigned now=buttons();int pressed=now && !input->previous;
    input->previous=now;return pressed;
}
static void present(const uint32_t *pixels,void *opaque){(void)opaque;xbox_FramebufferMovieFrame(pixels);}
static int play(const char *filename) {
    if(!getenv("RECOMP_FB_WINDOW"))return 0;
    char path[8192];
    if(snprintf(path,sizeof(path),"%s/%s",game_directory,filename)>=(int)sizeof(path))return 0;
    MovieInput input={buttons()};
    SsxMovieOutput output={present,skip,&input,1,1};
    int result=ssx_movie_play(path,&output);
    xbox_FramebufferMovieFrame(NULL);
    return result;
}
void ssx_movies_init(const char *directory) {
    if(strlen(directory)>=sizeof(game_directory))return;
    strcpy(game_directory,directory);ssx_register_startup_movie_player(play);
}
