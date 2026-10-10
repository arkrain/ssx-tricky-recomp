/* Test the actual OHCI register model without starting an interrupt thread. */
#define usb_gamepad_output usb_test_output
#define xbox_GetMemoryOffset usb_test_memory_offset
#define xbox_GetMappedSize usb_test_mapped_size
#define xbox_ContiguousAllocatedBytes usb_test_contiguous_bytes
#include "../third_party/xboxrecomp/src/usb/ohci.c"
#undef xbox_GetMemoryOffset
#undef xbox_ContiguousAllocatedBytes
#undef xbox_GetMappedSize
#include <assert.h>
static unsigned char dma_ram[4096];
size_t usb_test_mapped_size(void) { return 4 * 1024 * 1024; }
ptrdiff_t usb_test_memory_offset(void) { return (ptrdiff_t)(uintptr_t)dma_ram - XBOX_CONTIG_BASE; }
uint32_t usb_test_contiguous_bytes(void) { return sizeof dma_ram; }
recomp_func_t recomp_lookup(uint32_t va) { (void)va; return NULL; }
static unsigned output_calls, output_port;
static uint8_t last_output[6];
int usb_test_output(UsbGamepad *pad, const uint8_t *data, int length)
{
    assert(length==6 && data);
    output_calls++;output_port=pad->input_port;memcpy(last_output,data,6);return 6;
}
int main(void)
{
    uint32_t value=0xdeadbeef;
    assert(!xbox_OhciRead32(XBOX_OHCI0_BASE,&value) && value==0xdeadbeef);
    s_enabled=1;
    ohci_reset(&s_hc[0],XBOX_OHCI0_BASE,0);
    ohci_reset(&s_hc[1],XBOX_OHCI1_BASE,1);
    assert((s_hc[0].reg[HcRhDescriptorA/4]&255)==4);
    assert((s_hc[1].reg[HcRhDescriptorA/4]&255)==2);
    assert(s_hc[0].pads[0].input_port==2 && s_hc[0].pads[1].input_port==3);
    assert(s_hc[0].pads[2].input_port==0 && s_hc[0].pads[3].input_port==1);
    /* Host slots0/1 must enumerate as console controllers1/2, using root
     * ports3/4. Disconnecting slot1 must leave slot0's port/address intact. */
    ohci_update_connections(&s_hc[0],3);
    assert(!(s_hc[0].reg[HcRhPortStatus1/4]&PORT_CCS));
    assert(!(s_hc[0].reg[(HcRhPortStatus1+4)/4]&PORT_CCS));
    for(unsigned port=2;port<4;port++) {
        unsigned off=HcRhPortStatus1+port*4;
        assert(s_hc[0].reg[off/4]&PORT_CCS);
        xbox_OhciWrite32(XBOX_OHCI0_BASE+off,PORT_W_PRS_SET_RESET);
        assert(s_hc[0].default_port==port);
        assert(s_hc[0].reg[off/4]&PORT_PES);
        s_hc[0].pads[port].address=(uint8_t)(port+5);
    }
    ohci_update_connections(&s_hc[0],1);
    assert(s_hc[0].reg[(HcRhPortStatus1+8)/4]&PORT_PES);
    assert(s_hc[0].pads[2].address==7);
    assert(!(s_hc[0].reg[(HcRhPortStatus1+12)/4]&(PORT_CCS|PORT_PES)));
    assert(s_hc[0].reg[(HcRhPortStatus1+12)/4]&PORT_CSC);
    ohci_update_connections(&s_hc[0],15);
    for(unsigned port=0;port<4;port++)
        assert(s_hc[0].reg[(HcRhPortStatus1+port*4)/4]&PORT_CCS);
    /* HC1 exposes only its actual two ports, not the front panel. */
    xbox_OhciWrite32(XBOX_OHCI1_BASE+HcRhPortStatus1+8,PORT_W_PPS_SET_POWER);
    assert(s_hc[1].reg[(HcRhPortStatus1+8)/4]==0);
    ohci_reset(&s_hc[0],XBOX_OHCI0_BASE,0);
    assert(xbox_OhciRead32(XBOX_OHCI0_BASE,&value) && value==0x10);
    assert(xbox_OhciRead32(XBOX_OHCI1_BASE,&value) && value==0x10);
    assert(!xbox_OhciRead32(XBOX_OHCI0_BASE+1,&value));
    assert(!xbox_OhciWrite32(XBOX_OHCI0_BASE+XBOX_OHCI_SIZE,1));
    assert(xbox_OhciWrite32(XBOX_OHCI0_BASE+HcControl,0x80));
    assert(xbox_OhciRead32(XBOX_OHCI0_BASE+HcControl,&value) && value==0x80);
    xbox_OhciWrite32(XBOX_OHCI0_BASE+HcInterruptEnable,INTR_MIE|INTR_RHSC);
    xbox_OhciWrite32(XBOX_OHCI0_BASE+HcInterruptDisable,INTR_RHSC);
    assert(xbox_OhciRead32(XBOX_OHCI0_BASE+HcInterruptEnable,&value) && value==INTR_MIE);
    s_hc[0].reg[HcInterruptStatus/4]=INTR_RHSC|INTR_WDH;
    xbox_OhciWrite32(XBOX_OHCI0_BASE+HcInterruptStatus,INTR_RHSC);
    assert(xbox_OhciRead32(XBOX_OHCI0_BASE+HcInterruptStatus,&value) && value==INTR_WDH);
    /* Physical zero is a valid HCCA allocation; DMA must use its high mirror. */
    assert(guest_ptr(0, 256) == dma_ram);
    assert(guest_ptr(0x640, 16) == dma_ram + 0x640);
    assert(!guest_ptr(sizeof dma_ram - 1, 4));
    assert(guest_ok(0x40201674, 8));
    assert(!guest_ok(0x00201674, 8));
    assert(!guest_ok(0x40400000, 1));
    assert(!guest_ok(0xffffffff, 8));
    assert((uintptr_t)guest_ptr(0x40201674, 8) ==
           (uintptr_t)usb_test_memory_offset() + 0x201674);
    wr32(0x84, 0x12345678);
    assert(rd32(0x84) == 0x12345678);
    assert(!ohci_pad_for_address(&s_hc[0],0));
    s_hc[0].reg[HcRhPortStatus1/4]=PORT_CCS|PORT_PES;
    /* A real SETUP/IN/status chain must write both data and HCCA completion. */
    uint32_t ed=0x640, setup=0x700, in=0x710, status=0x720, tail=0x730;
    wr32(ed, 8u << 16); wr32(ed+4, tail); wr32(ed+8, setup);
    wr32(setup, 0xf0000000); wr32(setup+4, 0x800);
    wr32(setup+8, in); wr32(setup+12, 0x807);
    const uint8_t request[8]={0x80,6,0,1,0,0,8,0};
    memcpy(dma_ram+0x800, request, 8);
    wr32(in, 0xf0000000 | (TD_DP_IN << 19)); wr32(in+4, 0x820);
    wr32(in+8, status); wr32(in+12, 0x827);
    wr32(status, 0xf0000000 | (TD_DP_OUT << 19)); wr32(status+8, tail);
    s_hc[0].reg[HcControlHeadED/4]=ed;
    assert(ohci_run_control_list(&s_hc[0]) == 3);
    assert(dma_ram[0x820]==18 && dma_ram[0x821]==1);
    assert(rd32(ed+8)==tail && rd32(0x84)==status);
    assert(rd32(status+8)==in && rd32(in+8)==setup);
    /* Configuration replies span four eight-byte packets, then terminate
     * with a short packet, rather than replaying the first descriptor. */
    const uint8_t config_request[8]={0x80,6,0,2,0,0,80,0};
    memcpy(dma_ram+0x800, config_request, 8);
    wr32(setup+4,0x800);
    assert(ohci_do_td(&s_hc[0],0,setup)==0);
    for (unsigned i=0; i<4; ++i) {
        wr32(in+4,0x820+8*i); wr32(in+12,0x827+8*i);
        assert(ohci_do_td(&s_hc[0],0,in)==0);
    }
    assert(dma_ram[0x820]==9 && dma_ram[0x821]==2);
    assert(dma_ram[0x829]==9 && dma_ram[0x82a]==4);
    assert(dma_ram[0x832]==7 && dma_ram[0x834]==0x81);
    wr32(in+4,0x850); wr32(in+12,0x857);
    assert(ohci_do_td(&s_hc[0],0,in)==9);
    assert(rd32(in+4)==0x850); /* no bytes transferred */
    wr32(in,rd32(in) | (1u<<18));
    assert(ohci_do_td(&s_hc[0],0,in)==0); /* buffer rounding accepts short */
    /* A periodic ED overrides TD direction and writes a controller report. */
    memset(dma_ram,0,sizeof dma_ram);
    wr32(ed,(1u<<7) | (2u<<11) | (32u<<16));
    wr32(ed+4,tail); wr32(ed+8,in);
    wr32(in,0xf0000000 | (1u<<18) | (TD_DP_OUT<<19));
    wr32(in+4,0x820); wr32(in+8,tail); wr32(in+12,0x83f);
    wr32(3*4,ed); s_hc[0].reg[HcFmNumber/4]=3;
    assert(ohci_run_periodic_list(&s_hc[0])==1);
    assert(dma_ram[0x820]==0 && dma_ram[0x821]==20);
    assert(rd32(in+4)==0x834 && rd32(ed+8)==tail);
    uint8_t reply[32];
    UsbSetup xid={0xc1,6,0x4200,0,16};
    assert(usb_gamepad_control(&s_hc[0].pads[0],&xid,reply,sizeof reply)==16);
    assert(reply[1]==0x42 && reply[4]==1 && reply[6]==20);
    xid.bRequest=1; xid.wValue=0x0100; xid.wLength=20;
    assert(usb_gamepad_control(&s_hc[0].pads[0],&xid,reply,sizeof reply)==20);
    assert(reply[1]==20 && reply[4]==255);
    /* Address routing follows each enabled root port, including the most
     * recently reset device at address zero. Unknown addresses cannot read P1. */
    s_hc[0].reg[HcRhPortStatus1/4]=PORT_CCS|PORT_PES;
    s_hc[0].reg[(HcRhPortStatus1+4)/4]=PORT_CCS|PORT_PES;
    s_hc[0].pads[0].address=3;
    s_hc[0].pads[1].address=4;
    assert(ohci_pad_for_address(&s_hc[0],3)==&s_hc[0].pads[0]);
    assert(ohci_pad_for_address(&s_hc[0],4)==&s_hc[0].pads[1]);
    assert(!ohci_pad_for_address(&s_hc[0],5));
    /* Interleave different descriptor requests before either data stage. */
    wr32(setup,0);wr32(setup+12,0x807);
    memcpy(dma_ram+0x800,request,8);wr32(setup+4,0x800);
    assert(ohci_do_td(&s_hc[0],3,setup)==0);
    memcpy(dma_ram+0x800,config_request,8);wr32(setup+4,0x800);
    assert(ohci_do_td(&s_hc[0],4,setup)==0);
    wr32(in,TD_DP_IN<<19);wr32(in+4,0x820);wr32(in+12,0x827);
    assert(ohci_do_td(&s_hc[0],3,in)==0);
    assert(dma_ram[0x820]==18 && dma_ram[0x821]==1);
    wr32(in+4,0x820);
    assert(ohci_do_td(&s_hc[0],4,in)==0);
    assert(dma_ram[0x820]==9 && dma_ram[0x821]==2);
    /* Interrupt reports route by device address, independent of an in-flight
     * control request on another pad. */
    const uint8_t motor_report[6]={0,6,0x34,0x12,0xcd,0xab};
    memcpy(dma_ram+0x840,motor_report,6);
    wr32(status,TD_DP_OUT<<19);wr32(status+4,0x840);wr32(status+12,0x845);
    assert(ohci_do_td(&s_hc[0],3|(2<<7),status)==0);
    assert(output_calls==1 && output_port==2 && !memcmp(last_output,motor_report,6));
    wr32(status+4,0x840);
    assert(ohci_do_td(&s_hc[0],4|(2<<7),status)==0);
    assert(output_calls==2 && output_port==3);
    wr32(status+4,0x840);wr32(status+12,0x844);
    assert(ohci_do_td(&s_hc[0],3|(2<<7),status)==TD_CC_STALL);
    assert(ohci_do_td(&s_hc[0],3|(3<<7),status)==TD_CC_STALL);
    assert(output_calls==2);
    /* SET_REPORT data split across TDs must reach the motor backend once;
     * its zero-length IN status must not send it again. */
    const uint8_t set_output[8]={0x21,9,0,2,0,0,6,0};
    memcpy(dma_ram+0x800,set_output,8);wr32(setup+4,0x800);
    assert(ohci_do_td(&s_hc[0],4,setup)==0);
    for(unsigned i=0;i<2;i++) {
        wr32(status+4,0x840+i*3);wr32(status+12,0x842+i*3);
        assert(ohci_do_td(&s_hc[0],4,status)==0);
        assert(output_calls==2+i);
    }
    assert(output_port==3 && !memcmp(last_output,motor_report,6));
    wr32(in+4,0);wr32(in+12,0);
    assert(ohci_do_td(&s_hc[0],4,in)==0 && output_calls==3);
    /* An incomplete OUT report cannot succeed as an IN status. */
    wr32(setup+4,0x800);assert(ohci_do_td(&s_hc[0],4,setup)==0);
    wr32(status+4,0x840);wr32(status+12,0x842);
    assert(ohci_do_td(&s_hc[0],4,status)==0);
    assert(ohci_do_td(&s_hc[0],4,in)==TD_CC_STALL && output_calls==3);
    xbox_OhciWrite32(XBOX_OHCI0_BASE+HcRhPortStatus1+4,PORT_W_PRS_SET_RESET);
    assert(s_hc[0].pads[1].address==0);
    assert(ohci_pad_for_address(&s_hc[0],0)==&s_hc[0].pads[1]);
    assert(s_hc[0].pads[0].address==3);
    assert(!ohci_pad_for_address(&s_hc[0],4));
    s_hc[0].reg[HcRhPortStatus1/4]=0;
    assert(!ohci_pad_for_address(&s_hc[0],3));
    return 0;
}
