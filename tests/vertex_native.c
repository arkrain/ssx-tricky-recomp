#include "nv2a_vertex.h"
#include "nv2a_vertex_native.h"
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int reference_vertex_method(uint32_t,uint32_t);
int reference_vertex_execute(const float [16][4],float [13][4]);
static uint32_t seed=0x535358;
static float random_value(void){seed=seed*1664525u+1013904223u;return ((int)(seed>>16)-32768)/1024.f;}
static uint32_t u32(const unsigned char *p){return p[0]|(unsigned)p[1]<<8|(unsigned)p[2]<<16|(unsigned)p[3]<<24;}
static void method(unsigned m,unsigned v){ssx_vertex_method(m,v);reference_vertex_method(m,v);}
static void value(unsigned m,float f){unsigned bits;memcpy(&bits,&f,4);method(m,bits);}
static void upload(const uint32_t code[][4],unsigned n,unsigned start){
    method(0x1e9c,start);
    for(unsigned i=0;i<n;i++)for(unsigned k=0;k<4;k++)method(0xb00+k*4,code[i][k]);
    method(0x1ea0,start);
}
static void compare(float input[16][4]){
    float before[13][4],after[13][4],saved[16][4];
    memset(before,0x5a,sizeof before);memset(after,0x5a,sizeof after);memcpy(saved,input,sizeof saved);
    int a=reference_vertex_execute(input,before),b=ssx_vertex_execute(input,after);
    assert(a==b && !memcmp(before,after,sizeof before) && !memcmp(input,saved,sizeof saved));
    unsigned mask=ssx_vertex_input_mask();
    for(unsigned i=0;i<16;i++)if(!(mask&(1u<<i)))memset(saved[i],0,sizeof saved[i]);
    memset(after,0x5a,sizeof after);b=ssx_vertex_execute(saved,after);
    assert(a==b && !memcmp(before,after,sizeof before));
}
int main(int argc,char **argv){
    assert(argc==2);FILE *f=fopen(argv[1],"rb");assert(f);
    assert(!fseek(f,0,SEEK_END));long size=ftell(f);assert(size>4);rewind(f);
    unsigned char *data=malloc(size);assert(data && fread(data,1,size,f)==(size_t)size);fclose(f);
    method(0x1e94,6);unsigned programs=0,class_inputs=0;
    for(size_t offset=0;offset+4<=(size_t)size;offset+=4){
        unsigned h=u32(data+offset),n=h>>16;
        if((h&65535)!=0x2078 || !n || n>136 || offset+4+n*16>(size_t)size)continue;
        uint32_t code[136][4];
        for(unsigned i=0;i<n;i++)for(unsigned k=0;k<4;k++)code[i][k]=u32(data+offset+4+i*16+k*4);
        if(!ssx_vertex_native_find(code,n))continue;
        programs++;unsigned start=(programs%2 && n<128)?8:0;upload(code,n,start);
        for(unsigned trial=0;trial<128;trial++){
            float input[16][4];
            for(unsigned i=0;i<16;i++)for(unsigned k=0;k<4;k++)input[i][k]=random_value();
            if(trial%16==0){method(0x1ea4,0);for(unsigned i=0;i<192;i++)for(unsigned k=0;k<4;k++)value(0xb80+k*4,random_value());}
            if(trial%16==1)input[0][0]=NAN;
            if(trial%16==2)input[8][0]=INFINITY;
            if(trial%16==3)input[0][1]=3.402823466e38f;
            if(trial%16==4){method(0x1ea4,0);value(0xb80,NAN);}
            if(trial%16==5){method(0x1ea4,0);value(0xb80,1);}
            compare(input);
        }
        /* Preserve exact interpreter fallback for every binary32 class in
         * every input lane, including unused attributes and signaling NaNs. */
        {
            static const uint32_t special[]={
                0,0x80000000,1,0x80000001,0x007fffff,0x807fffff,
                0x00800000,0x80800000,0x3f800000,0xbf800000,
                0x7f7fffff,0xff7fffff,0x7f800000,0xff800000,
                0x7f800001,0xff800001,0x7fc00000,0xffc00000,
                0x7fffffff,0xffffffff
            };
            float input[16][4];
            for(unsigned i=0;i<16;i++)for(unsigned k=0;k<4;k++)input[i][k]=random_value();
            for(unsigned i=0;i<16;i++)for(unsigned k=0;k<4;k++) {
                float saved=input[i][k];
                for(unsigned j=0;j<sizeof special/sizeof special[0];j++) {
                    memcpy(&input[i][k],&special[j],4);compare(input);class_inputs++;
                }
                input[i][k]=saved;
            }
        }
        float input[16][4]={{1,2,3,1}};
        /* Changing only an input swizzle selects the generic path, then an
         * exact reload restores native eligibility. Partial upload is invalid. */
        code[0][2]^=1u<<2;upload(code,n,start);compare(input);
        code[0][2]^=1u<<2;upload(code,n,start);compare(input);
        method(0x1e9c,start);method(0xb00,code[0][0]);compare(input);
        upload(code,n,start);compare(input);
    }
    free(data);assert(programs>0);printf("Compared %u compiled vertex programs with the interpreter (%u class inputs)\n",programs,class_inputs);
    return 0;
}
