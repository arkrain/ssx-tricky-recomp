/* Exercise the Windows backend with ordinary virtual XInput state. */
#include "xinput_xbox.h"
#include <xinput.h>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
static int connected[4];
static XINPUT_STATE pads[4];
static XINPUT_VIBRATION last_vibration;
DWORD WINAPI XInputGetState(DWORD port,XINPUT_STATE *state)
{
    if(port>=4 || !connected[port])return ERROR_DEVICE_NOT_CONNECTED;
    *state=pads[port];return ERROR_SUCCESS;
}
DWORD WINAPI XInputSetState(DWORD port,XINPUT_VIBRATION *vibration)
{
    if(port>=4 || !connected[port])return ERROR_DEVICE_NOT_CONNECTED;
    last_vibration=*vibration;return ERROR_SUCCESS;
}
DWORD WINAPI XInputGetCapabilities(DWORD port,DWORD flags,XINPUT_CAPABILITIES *caps)
{
    (void)flags;
    if(port>=4 || !connected[port])return ERROR_DEVICE_NOT_CONNECTED;
    memset(caps,0,sizeof(*caps));caps->Type=XINPUT_DEVTYPE_GAMEPAD;
    caps->SubType=XINPUT_DEVSUBTYPE_GAMEPAD;return ERROR_SUCCESS;
}
static XBOX_THREAD_LOCAL int thread_value;
static DWORD WINAPI worker(void *unused){(void)unused;thread_value=91;return thread_value==91?0:1;}
int main(void)
{
    XBOX_INPUT_STATE state,other;
    XBOX_INPUT_CAPABILITIES caps;
    _putenv("RECOMP_FB_WINDOW=");xbox_InputInit();assert(!xbox_InputIsConnected(0));
    assert(xbox_InputGetState(0,&state)==ERROR_DEVICE_NOT_CONNECTED);
    connected[1]=1;pads[1].dwPacketNumber=17;pads[1].Gamepad.sThumbLX=1234;
    pads[1].Gamepad.wButtons=XINPUT_GAMEPAD_A;
    assert(xbox_InputIsConnected(1));
    connected[1]=0;assert(!xbox_InputIsConnected(1));connected[1]=1;
    _putenv("RECOMP_FB_WINDOW=1");xbox_InputInit();
    assert(xbox_InputIsConnected(0));assert(xbox_InputGetCapabilities(0,0,&caps)==ERROR_SUCCESS);
    assert(caps.Type==XINPUT_DEVTYPE_GAMEPAD && caps.SubType==XINPUT_DEVSUBTYPE_GAMEPAD);
    assert(xbox_InputGetState(0,&state)==ERROR_SUCCESS);DWORD packet=state.dwPacketNumber;
    int keys[]={VK_SPACE,VK_ESCAPE,VK_SHIFT,'R','V','C','Q','E',VK_RETURN,VK_BACK,VK_UP,VK_DOWN,VK_LEFT,VK_RIGHT,VK_LCONTROL,VK_RCONTROL,'W','A','I','J'};
    for(unsigned i=0;i<sizeof(keys)/sizeof(keys[0]);i++)xbox_InputKeyboardEvent(keys[i],1);
    assert(xbox_InputGetState(0,&state)==ERROR_SUCCESS);
    assert(state.dwPacketNumber!=packet && state.Gamepad.wButtons==0xff);
    for(int i=0;i<8;i++)assert(state.Gamepad.bAnalogButtons[i]==255);
    assert(state.Gamepad.sThumbLX==-32767 && state.Gamepad.sThumbLY==32767);
    assert(state.Gamepad.sThumbRX==-32767 && state.Gamepad.sThumbRY==32767);
    packet=state.dwPacketNumber;xbox_InputKeyboardEvent(VK_SPACE,1);
    assert(xbox_InputGetState(0,&state)==ERROR_SUCCESS && state.dwPacketNumber==packet);
    assert(xbox_InputGetState(1,&other)==ERROR_SUCCESS && other.dwPacketNumber==17);
    assert(other.Gamepad.wButtons==0 && other.Gamepad.bAnalogButtons[XBOX_BUTTON_A]==255);
    assert(other.Gamepad.sThumbLX==1234 && other.Gamepad.sThumbLY==0);
    xbox_InputKeyboardEvent('S',1);xbox_InputKeyboardEvent('D',1);
    xbox_InputKeyboardEvent('K',1);xbox_InputKeyboardEvent('L',1);
    assert(xbox_InputGetState(0,&state)==ERROR_SUCCESS);
    assert(state.Gamepad.sThumbLX==0 && state.Gamepad.sThumbLY==0);
    assert(state.Gamepad.sThumbRX==0 && state.Gamepad.sThumbRY==0);
    xbox_InputKeyboardEvent(-1,0);assert(xbox_InputGetState(0,&state)==ERROR_SUCCESS);
    XBOX_GAMEPAD empty={0};assert(!memcmp(&state.Gamepad,&empty,sizeof(empty)));
    connected[0]=1;pads[0].dwPacketNumber=31;pads[0].Gamepad.sThumbLX=2345;
    pads[0].Gamepad.wButtons=XINPUT_GAMEPAD_A;xbox_InputKeyboardEvent(VK_ESCAPE,1);
    assert(xbox_InputGetState(0,&state)==ERROR_SUCCESS);
    assert(state.Gamepad.sThumbLX==2345 && state.Gamepad.bAnalogButtons[0]==255 && state.Gamepad.bAnalogButtons[1]==255);
    xbox_InputKeyboardEvent(-1,0);assert(xbox_InputGetState(0,&state)==ERROR_SUCCESS);
    assert(state.Gamepad.bAnalogButtons[0]==255 && state.Gamepad.bAnalogButtons[1]==0);
    XBOX_VIBRATION vibration={123,456};assert(xbox_InputSetState(1,&vibration)==ERROR_SUCCESS);
    assert(last_vibration.wLeftMotorSpeed==123 && last_vibration.wRightMotorSpeed==456);
    assert(xbox_InputGetState(4,&state)==ERROR_DEVICE_NOT_CONNECTED);
    assert(xbox_InputGetCapabilities(0,0,NULL)==ERROR_DEVICE_NOT_CONNECTED);
    assert(xbox_InputGetState(0,NULL)==ERROR_DEVICE_NOT_CONNECTED);
    thread_value=7;HANDLE th=CreateThread(NULL,0,worker,NULL,0,NULL);assert(th);
    assert(WaitForSingleObject(th,5000)==WAIT_OBJECT_0);DWORD code=1;
    assert(GetExitCodeThread(th,&code) && code==0);CloseHandle(th);assert(thread_value==7);
    puts("Windows keyboard, focus release, gamepad merge/hotplug, vibration and TLS pass");return 0;
}
