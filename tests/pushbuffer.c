#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
static uint32_t ram[1024];
ptrdiff_t xbox_GetMemoryOffset(void) { return (ptrdiff_t)ram - 0x80000000u; }
static uint32_t methods[32], values[32], subchannels[32], count;
static volatile uint32_t progress;
static uint32_t observed[32];
static int patch_ahead;
void nv2a_pb_exec_report(void) {}
void nv2a_pb_exec_method(uint32_t sub, uint32_t method, uint32_t value)
{
  if (patch_ahead && method == 0x200) {
      /* Simulate the guest inserting a wake-up into a still queued NOOP. */
      ram[0x408/4] = 0x40100;
      ram[0x40c/4] = 0x310;
  }
  if (count < 32) { observed[count]=progress; methods[count]=method; values[count]=value; subchannels[count++]=sub; } }
#include "nv2a_pb_scan.c"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\n",__LINE__,#x); return 1; } } while(0)
#define VA(x) (0x80000000u+(x))
static void put(unsigned addr, uint32_t word) { ram[addr/4]=word; }
int main(void)
{
    setenv("RECOMP_PB_EXEC", "1", 1);
    /* Tail fence, ring jump, then head rendering state and another fence. */
    put(0x300, 0x41d70); put(0x304, 7); put(0x308, 0x101);
    put(0x100, 0x401a4); put(0x104, 8);
    put(0x108, 0x41d70); put(0x10c, 9);
    CHECK(nv2a_pb_scan(VA(0x300),VA(0x110)) == VA(0x110));
    CHECK(count==3 && methods[0]==0x1d70 && values[0]==7 && values[2]==9);
    /* A parameter that looks like a jump remains data across a PUT boundary. */
    put(0x110, (3<<18)|(2<<13)|0x200); put(0x114,0x301);
    put(0x118,0x302); put(0x11c,0x20000);
    CHECK(nv2a_pb_scan(VA(0x110),VA(0x118))==VA(0x118));
    CHECK(nv2a_pb_scan(VA(0x118),VA(0x120))==VA(0x120));
    CHECK(count==6 && methods[3]==0x200 && methods[5]==0x208 && values[5]==0x20000 && subchannels[4]==2);
    /* CALL/RETURN followed by an old-format jump, non-increasing parameters. */
    put(0x120,0x202); put(0x124,0x20000180);
    put(0x200,0x40080000|0x1808); put(0x204,11); put(0x208,12); put(0x20c,0x20000);
    put(0x180,0x41d70); put(0x184,13);
    CHECK(nv2a_pb_scan(VA(0x120),VA(0x188))==VA(0x188));
    CHECK(count==9 && methods[6]==0x1808 && methods[7]==0x1808 && values[8]==13);
    /* Invalid targets and reserved commands must not acknowledge PUT. */
    put(0x188,0x04000001);
    CHECK(nv2a_pb_scan(VA(0x188),VA(0x190))==VA(0x188));
    put(0x188,0xffffffff);
    CHECK(nv2a_pb_scan(VA(0x188),VA(0x190))==VA(0x188));
    CHECK(count==9);
    /* GET must expose progress during execution, not merely on return. */
    count=0; progress=0x400; patch_ahead=1;
    put(0x400,0x40200); put(0x404,77);
    put(0x408,0); put(0x40c,0);
    put(0x410,0x481); /* jump to another physical portion of the ring */
    put(0x480,0x40100); put(0x484,0x304);
    CHECK(nv2a_pb_scan_progress(VA(0x400),VA(0x488),&progress)==VA(0x488));
    CHECK(count==3 && values[1]==0x310 && values[2]==0x304);
    CHECK(observed[0]==0x404 && observed[1]==0x40c && observed[2]==0x484);
    CHECK(progress==0x488);
    put(0x488,0xffffffff);
    CHECK(nv2a_pb_scan_progress(VA(0x488),VA(0x490),&progress)==VA(0x488));
    CHECK(progress==0x488);
    return 0;
}
