#include "input/xinput_xbox.h"
#include <SDL.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
int main(void)
{
    extern const unsigned xbox_test_input_version;
    assert(xbox_test_input_version==2);
    XBOX_INPUT_STATE state;
    xbox_InputKeyboardEvent(SDL_SCANCODE_RETURN,1);
    xbox_InputKeyboardEvent(SDL_SCANCODE_DOWN,1);
    xbox_InputKeyboardEvent(SDL_SCANCODE_SPACE,1);
    assert(xbox_InputGetState(0,&state)==0);
    assert(state.Gamepad.wButtons==(XBOX_GAMEPAD_START|XBOX_GAMEPAD_DPAD_DOWN));
    assert(state.Gamepad.bAnalogButtons[XBOX_BUTTON_A]==255);
    xbox_InputKeyboardEvent(SDL_SCANCODE_RETURN,0);
    assert(xbox_InputGetState(0,&state)==0);
    assert(state.Gamepad.wButtons==XBOX_GAMEPAD_DPAD_DOWN);
    xbox_InputKeyboardEvent(-1,0);
    assert(xbox_InputGetState(0,&state)==0);
    assert(state.Gamepad.wButtons==0 && state.Gamepad.bAnalogButtons[XBOX_BUTTON_A]==0);
    xbox_InputKeyboardEvent(SDL_SCANCODE_W,1);
    xbox_InputKeyboardEvent(SDL_SCANCODE_A,1);
    xbox_InputGetState(0,&state);
    assert(state.Gamepad.sThumbLX==-32767 && state.Gamepad.sThumbLY==32767);
    xbox_InputKeyboardEvent(SDL_SCANCODE_D,1);
    xbox_InputGetState(0,&state);assert(state.Gamepad.sThumbLX==0);
    xbox_InputKeyboardEvent(-1,0);
    xbox_InputGetState(0,&state);
    assert(state.Gamepad.sThumbLX==0 && state.Gamepad.sThumbLY==0);
    /* Every Xbox analog button and both stick clicks have a keyboard path. */
    int keys[8]={SDL_SCANCODE_SPACE,SDL_SCANCODE_ESCAPE,SDL_SCANCODE_LSHIFT,
        SDL_SCANCODE_R,SDL_SCANCODE_C,SDL_SCANCODE_V,SDL_SCANCODE_Q,SDL_SCANCODE_E};
    for(int i=0;i<8;i++)xbox_InputKeyboardEvent(keys[i],1);
    xbox_InputKeyboardEvent(SDL_SCANCODE_LCTRL,1);
    xbox_InputKeyboardEvent(SDL_SCANCODE_RCTRL,1);
    xbox_InputKeyboardEvent(SDL_SCANCODE_I,1);
    xbox_InputKeyboardEvent(SDL_SCANCODE_L,1);
    xbox_InputGetState(0,&state);
    for(int i=0;i<8;i++)assert(state.Gamepad.bAnalogButtons[i]==255);
    assert(state.Gamepad.wButtons==(XBOX_GAMEPAD_LEFT_THUMB|XBOX_GAMEPAD_RIGHT_THUMB));
    assert(state.Gamepad.sThumbRX==32767 && state.Gamepad.sThumbRY==32767);
    xbox_InputKeyboardEvent(SDL_SCANCODE_J,1);
    xbox_InputKeyboardEvent(SDL_SCANCODE_K,1);
    xbox_InputGetState(0,&state);
    assert(state.Gamepad.sThumbRX==0 && state.Gamepad.sThumbRY==0);
    xbox_InputKeyboardEvent(-1,0);xbox_InputGetState(0,&state);
    for(int i=0;i<8;i++)assert(state.Gamepad.bAnalogButtons[i]==0);
    assert(state.Gamepad.wButtons==0);
    /* The live probe file drives the same report and releases on deletion. */
    char path[]="/tmp/ssx-keyboard-test-XXXXXX";
    int fd=mkstemp(path);assert(fd>=0);
    FILE *f=fdopen(fd,"w");assert(f);fputs("90100\n",f);fclose(f);
    setenv("RECOMP_TEST_INPUT_FILE",path,1);
    xbox_InputInit();SDL_Delay(20);
    assert(xbox_InputGetState(0,&state)==0 && state.Gamepad.bAnalogButtons[XBOX_BUTTON_A]==255);
    assert(state.Gamepad.sThumbLX==32767 && state.Gamepad.sThumbLY==32767);
    /* Optional analog probe axes preserve exact signed controller values. */
    f=fopen(path,"w");assert(f);fputs("100 -32768 32767 -12345 23456\n",f);fclose(f);
    SDL_Delay(20);assert(xbox_InputGetState(0,&state)==0);
    assert(state.Gamepad.bAnalogButtons[XBOX_BUTTON_A]==255);
    assert(state.Gamepad.sThumbLX==-32768 && state.Gamepad.sThumbLY==32767);
    assert(state.Gamepad.sThumbRX==-12345 && state.Gamepad.sThumbRY==23456);
    xbox_InputKeyboardEvent(SDL_SCANCODE_D,1);
    xbox_InputKeyboardEvent(SDL_SCANCODE_S,1);
    xbox_InputGetState(0,&state);
    assert(state.Gamepad.sThumbLX==32767 && state.Gamepad.sThumbLY==-32767);
    assert(state.Gamepad.sThumbRX==-12345 && state.Gamepad.sThumbRY==23456);
    xbox_InputKeyboardEvent(-1,0);
    const char *invalid[]={"", "xyz", "-100", "1000000", "100x", "100 1",
        "100 1 2 3", "100 1 2 3 4 5", "100 32768 0 0 0",
        "100 -32769 0 0 0", "100 999999999999999999999999 0 0 0",
        "100 0 0 0 NaN", "100 0x10 0 0 0", "100 0 0 0 0x",
        "100 1 2 3 4\n100\n"};
    for(unsigned i=0;i<sizeof invalid/sizeof invalid[0];i++) {
        f=fopen(path,"w");assert(f);fputs("200 8192 -16384 32767 -32768\n",f);fclose(f);
        SDL_Delay(20);xbox_InputGetState(0,&state);
        assert(state.Gamepad.bAnalogButtons[XBOX_BUTTON_B]==255);
        assert(state.Gamepad.sThumbLX==8192 && state.Gamepad.sThumbLY==-16384);
        assert(state.Gamepad.sThumbRX==32767 && state.Gamepad.sThumbRY==-32768);
        f=fopen(path,"w");assert(f);fputs(invalid[i],f);fclose(f);
        SDL_Delay(20);xbox_InputGetState(0,&state);
        assert(!state.Gamepad.wButtons);
        for(int k=0;k<8;k++)assert(!state.Gamepad.bAnalogButtons[k]);
        assert(!state.Gamepad.sThumbLX && !state.Gamepad.sThumbLY);
        assert(!state.Gamepad.sThumbRX && !state.Gamepad.sThumbRY);
    }
    /* Returning to the legacy mask releases the prior analog steering. */
    f=fopen(path,"w");assert(f);fputs("100\n",f);fclose(f);
    SDL_Delay(20);xbox_InputGetState(0,&state);
    assert(state.Gamepad.bAnalogButtons[XBOX_BUTTON_A]==255);
    assert(!state.Gamepad.sThumbLX && !state.Gamepad.sThumbLY);
    assert(!state.Gamepad.sThumbRX && !state.Gamepad.sThumbRY);
    unlink(path);SDL_Delay(20);
    assert(xbox_InputGetState(0,&state)==0 && state.Gamepad.bAnalogButtons[XBOX_BUTTON_A]==0);
    assert(state.Gamepad.sThumbLX==0 && state.Gamepad.sThumbLY==0);
    return 0;
}
