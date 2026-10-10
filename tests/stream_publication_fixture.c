/* Execute the audited translated producer with a forced reader at each store.
 * Generated function is supplied by the fixture runner. No guest logic is mocked.
 * Kernel Enter/Leave stubs only account for the original return/lock contract. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
static uint32_t memory[0x10000/4],eax,ebx,ecx,edx,esi,edi,esp,g_ebp,g_seh_ebp;
static unsigned lock_depth,publications;
static uint32_t stream=0x2100,expected_pointer,expected_count;
#define LO8(v) ((uint8_t)(v))
#define SET_LO8(v,x) ((v)=((v)&0xffffff00u)|(uint8_t)(x))
#define CMP_EQ(a,b) ((a)==(b))
#define CMP_NE(a,b) ((a)!=(b))
#define CMP_L(a,b) ((int32_t)(a)<(int32_t)(b))
#define CMP_LE(a,b) ((int32_t)(a)<=(int32_t)(b))
#define CMP_GE(a,b) ((int32_t)(a)>=(int32_t)(b))
#define CMP_A(a,b) ((uint32_t)(a)>(uint32_t)(b))
#define TEST_Z(a,b) (((a)&(b))==0)
#define TEST_NZ(a,b) (((a)&(b))!=0)
static uint32_t read32(uint32_t address){assert(!(address&3) && address<sizeof memory);return memory[address/4];}
static void store32(uint32_t address,uint32_t value){
 assert(!(address&3) && address<sizeof memory);memory[address/4]=value;
 if(address==stream+8 && value){
  ++publications;
  /* A reader can run immediately after publication, before the next store. */
  assert(lock_depth==1);assert(value==expected_count);
  if(read32(stream+12)!=expected_pointer){fprintf(stderr,"reader observed count=%u pointer=%08x, expected=%08x\n",value,read32(stream+12),expected_pointer);exit(42);}
 }
}
#define RECOMP_READ32(a) read32(a)
#define RECOMP_STORE32(a,v) store32(a,v)
#define PUSH32(s,v) do{(s)-=4;store32(s,v);}while(0)
#define POP32(s,v) do{(v)=read32(s);(s)+=4;}while(0)
#define RECOMP_ABI_CALL(a,fn) fn()
static void sub_00164300(void){++lock_depth;esp+=4;}
static void sub_00164310(void){assert(lock_depth==1);--lock_depth;esp+=4;}
#ifndef PRODUCER_INCLUDE
#error Need extracted translated producer
#endif
#include PRODUCER_INCLUDE
static void fixture(unsigned append,unsigned cancel,unsigned incomplete,unsigned unmatched){
 memset(memory,0,sizeof memory);eax=0x1000;ebx=0x81;ecx=0x82;edx=0x83;esi=0x84;edi=0x85;esp=0x9000;g_ebp=g_seh_ebp=0x87;lock_depth=publications=0;
 memory[esp/4]=0x12345678;
 memory[(eax+0x64)/4]=0x3000+(incomplete?0x20:0x40);memory[(eax+0x60)/4]=0x3000;memory[(eax+0x6c)/4]=0x4000;
 memory[(eax+0x2c)/4]=1;memory[(eax+0x28)/4]=0x2000;memory[(eax+0x30)/4]=stream;
 memory[0x2000/4]=unmatched?0x2222:0x1111;memory[(0x2000+4)/4]=0x1111;memory[(0x2000+8)/4]=1;
 memory[0x3000/4]=0x1111;memory[(0x3000+4)/4]=0x40;memory[(0x4000+0x11c)/4]=0x9999;memory[(0x4000+4)/4]=cancel?4:0;
 expected_pointer=append?0x2fc0:0x3000;expected_count=0x40+(append?0x20:0);memory[(stream+8)/4]=append?0x20:0;memory[(stream+12)/4]=append?0x2fc0:0xdeadc0ed;
 sub_0014EA00();assert(esp==0x9004 && !lock_depth);assert(ebx==0x81 && esi==0x84 && edi==0x85);
 if(cancel || incomplete || unmatched){assert(publications==0);assert(read32(stream+8)==(append?0x20:0));}
 else {assert(publications==1);assert(read32(stream+8)==expected_count && read32(stream+12)==expected_pointer);assert(read32(0x1000+0x60)==0x3040);assert(read32(0x3000+4)==0x01000040);}
}
int main(void){fixture(0,0,0,0);fixture(1,0,0,0);fixture(0,1,0,0);fixture(0,0,1,0);fixture(0,0,0,1);puts("actual producer: first packet/append/cancel/incomplete/unmatched contracts pass");}
