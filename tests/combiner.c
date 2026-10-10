#include "nv2a_combiner.h"
#include <assert.h>
#include <stdlib.h>
int main(void) {
    uint32_t s[0x2000/4]={0};
    float d[4]={0.5f,1,0.25f,0.5f},v[4]={0,0,0,1};
    float t[4][4]={{1,0.5f,1,0.5f},{0},{0},{0}};
    s[0x1e60/4]=0x11101;s[0xac0/4]=0xc8c40000;s[0x260/4]=0xd8d41010;
    s[0x1e40/4]=s[0xaa0/4]=0xc0;s[0x288/4]=0x150c0500;s[0x28c/4]=0x1c80;
    assert(rc_fragment(s,d,v,t)==0x40808040);
    /* An inert second stage forces the general evaluator, same expected pixel. */
    s[0x1e60/4]=0x11102;
    assert(rc_fragment(s,d,v,t)==0x40808040);
    v[0]=1;v[3]=0.5f;
    assert(rc_fragment(s,d,v,t)==0x40bf4020);
    s[0x1e60/4]=0x11101;
    assert(rc_fragment(s,d,v,t)==0x40bf4020);
    /* A final combiner can select a constant without using texture colour. */
    s[0x288/4]=1;s[0x28c/4]=0x1100;s[0x1e20/4]=0xff123456;
    assert(rc_fragment(s,d,v,t)==0xff123456);
    /* Four-stage menu material: expanded normal dot product lights the
     * texture pair; stage-three pass-through provides the fog colour. */
    memset(s,0,sizeof s);
    s[0x1e60/4]=0x11104;s[0x288/4]=0x1b080b00;s[0x28c/4]=0x1880;
    uint32_t rgb[4]={0x4a44c8c9,0xc820c8c5,0xc1c80000,0x0dc8cc2d};
    uint32_t alpha[4]={0x5a54d8d9,0xd830d8d5,0xd1d81010,0x1dd8dc3d};
    uint32_t rgbout[4]={0x820d8,0x800,0xc0,0x800};
    uint32_t aout[4]={8,0x800,0xc0,0x800};
    for(int i=0;i<4;i++){s[0xac0/4+i]=rgb[i];s[0x260/4+i]=alpha[i];s[0x1e40/4+i]=rgbout[i];s[0xaa0/4+i]=aout[i];}
    s[0xa60/4+2]=0xff9999b2;
    float normal[4]={1,0.5f,0.5f,1}, spec[4]={0.4f,0.4f,0.4f,0};
    float material[4][4]={{0.5f,0.25f,1,1},{0.5f,1,0.5f,1},{1,0.5f,0.5f,1},{0.7f,0.7f,0.8f,1}};
    assert(rc_fragment(s,normal,spec,material)==0xff5959b3u);
    for(int i=0;i<10;i++) {
        normal[0]=i/9.f;material[3][3]=1-i/9.f;
        s[0x1e60/4]=0x11104;
        uint32_t fast=rc_fragment(s,normal,spec,material);
        s[0x1e60/4]=0x11105; /* inert fifth stage forces generic evaluation */
        uint32_t general=rc_fragment(s,normal,spec,material);
        for(int k=0;k<4;k++) assert(abs((int)((fast>>(k*8))&255)-(int)((general>>(k*8))&255))<=1);
    }
    return 0;
}
