/* Exercise USB wire reports with distinguishable host controller states. */
#include "../third_party/xboxrecomp/src/usb/usb_gamepad.c"
#include <assert.h>
DWORD xbox_InputGetState(DWORD port, XBOX_INPUT_STATE *state)
{
    memset(state,0,sizeof *state);
    state->Gamepad.wButtons=(WORD)(1u<<port);
    state->Gamepad.bAnalogButtons[port]=255;
    state->Gamepad.sThumbLX=(SHORT)(port ? -12345 : 23456);
    return port<2 ? ERROR_SUCCESS : ERROR_DEVICE_NOT_CONNECTED;
}
static unsigned motor_calls, motor_port;
static XBOX_VIBRATION motors;
DWORD xbox_InputSetState(DWORD port, const XBOX_VIBRATION *vibration)
{
    motor_calls++; motor_port=port; motors=*vibration; return ERROR_SUCCESS;
}
int main(void)
{
    UsbGamepad pads[2]={{.input_port=0},{.input_port=1}};
    uint8_t report[2][20];
    for(unsigned p=0;p<2;p++) {
        UsbSetup address={0,5,(uint16_t)(p+3),0,0};
        UsbSetup config={0,9,(uint16_t)(p+1),0,0};
        assert(usb_gamepad_control(&pads[p],&address,report[p],20)==0);
        assert(usb_gamepad_control(&pads[p],&config,report[p],20)==0);
    }
    assert(pads[0].address==3 && pads[1].address==4);
    for(unsigned p=0;p<2;p++) {
        UsbSetup config={0x80,8,0,0,1};
        assert(usb_gamepad_control(&pads[p],&config,report[p],20)==1);
        assert(report[p][0]==p+1);
        assert(usb_gamepad_report(&pads[p],report[p],20)==20);
        assert(report[p][2]==(1u<<p));
        assert(report[p][4+p]==255 && report[p][5-p]==0);
        int16_t x=(int16_t)(report[p][12]|report[p][13]<<8);
        assert(x==(p ? -12345 : 23456));
    }
    UsbSetup get_report={0xa1,1,0x0100,0,20};
    uint8_t control[20];
    assert(usb_gamepad_control(&pads[1],&get_report,control,20)==20);
    assert(memcmp(control,report[1],20)==0);
    uint8_t output[6]={0,6,0x34,0x12,0xcd,0xab};
    for(unsigned p=0;p<2;p++) {
        assert(usb_gamepad_output(&pads[p],output,6)==6);
        assert(motor_calls==p+1 && motor_port==p);
        assert(motors.wLeftMotorSpeed==0x1234 && motors.wRightMotorSpeed==0xabcd);
    }
    for(int len=0;len<6;len++) assert(usb_gamepad_output(&pads[0],output,len)==-1);
    assert(usb_gamepad_output(&pads[0],output,7)==-1);
    assert(usb_gamepad_output(&pads[0],NULL,6)==-1);
    output[0]=1; assert(usb_gamepad_output(&pads[0],output,6)==-1);
    output[0]=0;output[1]=5;assert(usb_gamepad_output(&pads[0],output,6)==-1);
    assert(motor_calls==2);
    output[1]=6;memset(output+2,0,4);
    assert(usb_gamepad_output(&pads[1],output,6)==6);
    assert(motor_port==1 && !motors.wLeftMotorSpeed && !motors.wRightMotorSpeed);
    return 0;
}
