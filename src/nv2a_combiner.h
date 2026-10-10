/* CPU evaluation of NV register combiners. Register encodings are NV097 wire
 * fields; equations follow GL_NV_register_combiners (Khronos registry).
 * This evaluator is independent of the host graphics API. */
#ifndef SSX_NV2A_COMBINER_H
#define SSX_NV2A_COMBINER_H
#include <stdint.h>
#include <string.h>
#include <math.h>
static float rc_clamp(float x,float lo,float hi) { return x<lo?lo:(x>hi?hi:x); }
static void rc_unpack(uint32_t c,float v[4]) {
    v[0]=((c>>16)&255)/255.f; v[1]=((c>>8)&255)/255.f;
    v[2]=(c&255)/255.f; v[3]=(c>>24)/255.f;
}
static uint32_t rc_pack(const float v[4]) {
    uint32_t c[4]; for(int k=0;k<4;k++) c[k]=(uint32_t)(rc_clamp(v[k],0,1)*255.f+0.5f);
    return c[3]<<24 | c[0]<<16 | c[1]<<8 | c[2];
}
static float rc_input(const float r[16][4],unsigned wire,int component,int alpha) {
    unsigned chan=(wire&16) ? 3 : (alpha ? 2 : component);
    float x=r[wire&15][chan], u=x>0?x:0;
    switch((wire>>5)&7) {
    case 0:return u; case 1:return 1-rc_clamp(x,0,1);
    case 2:return 2*u-1; case 3:return 1-2*u;
    case 4:return u-0.5f; case 5:return 0.5f-u;
    case 6:return x; default:return -x;
    }
}
static float rc_output(float x,unsigned wire) {
    unsigned map=(wire>>15)&7;
    if(map&1) x-=0.5f;
    if((map&6)==2) x*=2;
    if((map&6)==4) x*=4;
    if((map&6)==6) x*=0.5f;
    return rc_clamp(x,-1,1);
}
static void rc_half(const float r[16][4],float next[16][4],uint32_t in,uint32_t out,int alpha,unsigned ctl) {
    float v[4][3], ab[3],cd[3];
    int n=alpha?1:3;
    for(int i=0;i<4;i++) for(int k=0;k<n;k++) v[i][k]=rc_input(r,in>>(24-8*i),k,alpha);
    for(int k=0;k<n;k++) {ab[k]=v[0][k]*v[1][k];cd[k]=v[2][k]*v[3][k];}
    if(!alpha && (out&(1u<<13))) { float dot=ab[0]+ab[1]+ab[2];for(int k=0;k<3;k++)ab[k]=dot; }
    if(!alpha && (out&(1u<<12))) { float dot=cd[0]+cd[1]+cd[2];for(int k=0;k<3;k++)cd[k]=dot; }
    int select=(ctl&0x100) ? r[12][3]>=0.5f : ((int)(rc_clamp(r[12][3],0,1)*255)&1);
    unsigned dest[3]={(out>>4)&15,out&15,(out>>8)&15};
    for(int k=0;k<n;k++) {
        float values[3]={ab[k],cd[k],(out&(1u<<14)) ? (select?cd[k]:ab[k]) : ab[k]+cd[k]};
        for(int j=0;j<3;j++) if(dest[j]) next[dest[j]][alpha?3:k]=rc_output(values[j],out);
    }
    if(!alpha) {
        if((out&(1u<<19)) && dest[0]) next[dest[0]][3]=rc_output(ab[2],out);
        if((out&(1u<<18)) && dest[1]) next[dest[1]][3]=rc_output(cd[2],out);
    }
}
static uint32_t rc_fragment(const uint32_t *state,const float diffuse[4],const float specular[4],const float tex[4][4]) {
    /* Common fixed-function programs emitted by this title. These equations
     * are exactly the one-stage general combiner plus the final fog blend. */
    if ((state[0x1e60/4]&15)==1 && state[0x1e40/4]==0xc0 && state[0xaa0/4]==0xc0 &&
        state[0x288/4]==0x150c0500 && state[0x28c/4]==0x1c80 &&
        ((state[0xac0/4]==0xc8c40000 && state[0x260/4]==0xd8d41010) ||
         (state[0xac0/4]==0xc4200000 && state[0x260/4]==0xd4301010))) {
        int textured=state[0xac0/4]==0xc8c40000;
        float v[4],fog=specular[3]>0?specular[3]:0;
        for(int k=0;k<4;k++) v[k]=rc_clamp(diffuse[k]*(textured?tex[0][k]:1),0,1);
        for(int k=0;k<3;k++) v[k]=fog*v[k]+(1-fog)*(specular[k]>0?specular[k]:0);
        return rc_pack(v);
    }
    if (state[0x1e60/4]==0x11104 && state[0x288/4]==0x1b080b00 && state[0x28c/4]==0x1880) {
        static const uint32_t ci[4]={0x4a44c8c9,0xc820c8c5,0xc1c80000,0x0dc8cc2d};
        static const uint32_t ai[4]={0x5a54d8d9,0xd830d8d5,0xd1d81010,0x1dd8dc3d};
        static const uint32_t co[4]={0x820d8,0x800,0xc0,0x800},ao[4]={8,0x800,0xc0,0x800};
        if (!memcmp(state+0xac0/4,ci,sizeof ci) && !memcmp(state+0x260/4,ai,sizeof ai) &&
            !memcmp(state+0x1e40/4,co,sizeof co) && !memcmp(state+0xaa0/4,ao,sizeof ao)) {
            float dot=0,c[4],result[4];
            for(int k=0;k<3;k++) dot+=(2*fmaxf(0,tex[2][k])-1)*(2*fmaxf(0,diffuse[k])-1);
            float light=rc_clamp(dot,0,1),fog=fmaxf(0,tex[3][3]);
            rc_unpack(state[0xa60/4+2],c);
            for(int k=0;k<4;k++) {
                float base=rc_clamp(tex[0][k]*tex[1][k],-1,1);
                base=rc_clamp(base*(1+specular[k]),-1,1);
                float ambient=rc_clamp(c[k]*base,-1,1);
                result[k]=rc_clamp(light*base+(1-light)*ambient,-1,1);
                result[k]=fmaxf(0,result[k]);
                if(k<3)result[k]=fog*result[k]+(1-fog)*fmaxf(0,tex[3][k]);
            }
            return rc_pack(result);
        }
    }
    float r[16][4]={{0}},next[16][4];
    memcpy(r[4],diffuse,16); memcpy(r[5],specular,16); memcpy(r[8],tex,64);
    r[12][3]=tex[0][3];
    unsigned ctl=state[0x1e60/4],count=ctl&15; if(count>8)count=8;
    for(unsigned stage=0;stage<count;stage++) {
        rc_unpack(state[0xa60/4+((ctl&0x1000)?stage:0)],r[1]);
        rc_unpack(state[0xa80/4+((ctl&0x10000)?stage:0)],r[2]);
        memcpy(next,r,sizeof r);
        rc_half(r,next,state[0xac0/4+stage],state[0x1e40/4+stage],0,ctl);
        rc_half(r,next,state[0x260/4+stage],state[0xaa0/4+stage],1,ctl);
        memcpy(r,next,sizeof r);
    }
    uint32_t f0=state[0x288/4],f1=state[0x28c/4];
    if(!(f0|f1))return rc_pack(r[12]);
    rc_unpack(state[0x1e20/4],r[1]);rc_unpack(state[0x1e24/4],r[2]);
    for(int k=0;k<3;k++) {
        r[14][k]=((f1&0x40)?1-r[5][k]:r[5][k])+((f1&0x20)?1-r[12][k]:r[12][k]);
        if(f1&0x80)r[14][k]=rc_clamp(r[14][k],0,1);
        r[15][k]=rc_input(r,f1>>24,k,0)*rc_input(r,f1>>16,k,0);
    }
    float result[4];
    for(int k=0;k<3;k++) {
        float a=rc_input(r,f0>>24,k,0),b=rc_input(r,f0>>16,k,0);
        float c=rc_input(r,f0>>8,k,0),d=rc_input(r,f0,k,0);
        result[k]=a*b+(1-a)*c+d;
    }
    result[3]=rc_input(r,f1>>8,0,1);
    return rc_pack(result);
}
#endif
