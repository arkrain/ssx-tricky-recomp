#include "input/xinput_xbox.h"
#include <SDL.h>
#include <assert.h>
static unsigned motor_calls[3];
static Uint16 low[3],high[3];
static int SDLCALL rumble(void *userdata, Uint16 l, Uint16 h)
{
    unsigned slot=(unsigned)(uintptr_t)userdata;
    motor_calls[slot]++;low[slot]=l;high[slot]=h;return 0;
}
static SDL_Joystick *attach(int *index, unsigned slot)
{
    SDL_VirtualJoystickDesc desc={0};
    desc.version=SDL_VIRTUAL_JOYSTICK_DESC_VERSION;
    desc.type=SDL_JOYSTICK_TYPE_GAMECONTROLLER;
    desc.naxes=SDL_CONTROLLER_AXIS_MAX;desc.nbuttons=SDL_CONTROLLER_BUTTON_MAX;
    desc.userdata=(void *)(uintptr_t)slot;desc.Rumble=rumble;
    *index=SDL_JoystickAttachVirtualEx(&desc);
    assert(*index>=0 && SDL_IsGameController(*index));
    SDL_Joystick *joy=SDL_JoystickOpen(*index);assert(joy);return joy;
}
int main(void)
{
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS,"1");
    xbox_InputInit();
    XBOX_INPUT_STATE state;
    int first,second;SDL_Joystick *a=attach(&first,0),*b=attach(&second,1);
    assert(SDL_JoystickSetVirtualButton(a,SDL_CONTROLLER_BUTTON_A,1)==0);
    assert(SDL_JoystickSetVirtualAxis(a,SDL_CONTROLLER_AXIS_LEFTX,12345)==0);
    assert(SDL_JoystickSetVirtualAxis(a,SDL_CONTROLLER_AXIS_LEFTY,-23456)==0);
    assert(SDL_JoystickSetVirtualButton(b,SDL_CONTROLLER_BUTTON_B,1)==0);
    SDL_Delay(300);SDL_JoystickUpdate();
    assert(xbox_InputGetState(0,&state)==0);
    assert(state.Gamepad.bAnalogButtons[XBOX_BUTTON_A]==255);
    assert(state.Gamepad.sThumbLX==12345 && state.Gamepad.sThumbLY==23455);
    assert(xbox_InputGetState(1,&state)==0 && state.Gamepad.bAnalogButtons[XBOX_BUTTON_B]==255);
    XBOX_VIBRATION motors={0x1234,0xabcd};
    assert(xbox_InputSetState(0,&motors)==0);
    assert(low[0]==0x1234 && high[0]==0xabcd && !motor_calls[1]);
    motors.wLeftMotorSpeed=0xffff;motors.wRightMotorSpeed=1;
    assert(xbox_InputSetState(1,&motors)==0);
    assert(low[1]==0xffff && high[1]==1 && motor_calls[0]==1);
    motors.wLeftMotorSpeed=motors.wRightMotorSpeed=0;
    assert(xbox_InputSetState(0,&motors)==0);
    assert(!low[0] && !high[0]);
    SDL_JoystickClose(a);assert(SDL_JoystickDetachVirtual(first)==0);
    SDL_Delay(300);SDL_JoystickUpdate();
    assert(xbox_InputGetState(0,&state)==0 && !state.Gamepad.bAnalogButtons[XBOX_BUTTON_A]);
    assert(!xbox_InputIsConnected(0));
    /* The remaining pad must keep its player slot after device indices shift. */
    assert(xbox_InputGetState(1,&state)==0 && state.Gamepad.bAnalogButtons[XBOX_BUTTON_B]==255);
    int replacement;SDL_Joystick *c=attach(&replacement,2);
    assert(SDL_JoystickSetVirtualButton(c,SDL_CONTROLLER_BUTTON_Y,1)==0);
    SDL_Delay(300);SDL_JoystickUpdate();
    assert(xbox_InputGetState(0,&state)==0 && state.Gamepad.bAnalogButtons[XBOX_BUTTON_Y]==255);
    assert(xbox_InputGetState(1,&state)==0 && state.Gamepad.bAnalogButtons[XBOX_BUTTON_B]==255);
    motors.wLeftMotorSpeed=17;motors.wRightMotorSpeed=33;
    assert(xbox_InputSetState(0,&motors)==0 && low[2]==17 && high[2]==33);
    assert(low[1]==0xffff && high[1]==1);
    SDL_JoystickClose(c);SDL_JoystickDetachVirtual(replacement);
    SDL_JoystickClose(b);SDL_Quit();return 0;
}
