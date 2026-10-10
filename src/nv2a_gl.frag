#version 330 core
in vec4 diffuse;
in vec4 specular;
in vec4 texcoord[4];
out vec4 color;
uniform sampler2D tex0,tex1,tex2,tex3;
uniform ivec4 texMode,texValid,texNormalized;
uniform vec2 texSize[4];
uniform uint control;
uniform uvec4 combiner[8]; // RGB input, alpha input, RGB output, alpha output
uniform vec4 constant0[8],constant1[8],final0,final1;
uniform uvec2 finalInput;
uniform int alphaEnable,alphaFunc,alphaRef;
uniform int legacyTextured;
uniform int windowExclusive;
uniform ivec4 windowRect[8]; // minX, minY, maxX, maxY, inclusive
float inputValue(vec4 r[16],uint wire,int component,bool alpha) {
    int chan=(wire&16u)!=0u?3:(alpha?2:component);
    float x=r[int(wire&15u)][chan],u=max(x,0.0);
    uint mode=(wire>>5u)&7u;
    if(mode==0u)return u;
    if(mode==1u)return 1.0-clamp(x,0.0,1.0);
    if(mode==2u)return 2.0*u-1.0;
    if(mode==3u)return 1.0-2.0*u;
    if(mode==4u)return u-0.5;
    if(mode==5u)return 0.5-u;
    if(mode==6u)return x;
    return -x;
}
float outputValue(float x,uint wire) {
    uint m=(wire>>15u)&7u;
    if((m&1u)!=0u)x-=0.5;
    if((m&6u)==2u)x*=2.0;
    if((m&6u)==4u)x*=4.0;
    if((m&6u)==6u)x*=0.5;
    return clamp(x,-1.0,1.0);
}
void halfStage(vec4 r[16],inout vec4 next[16],uint ins,uint outs,bool alpha) {
    vec3 v[4];
    for(int i=0;i<4;i++)for(int k=0;k<3;k++)v[i][k]=inputValue(r,ins>>uint(24-8*i),k,alpha);
    vec3 ab=v[0]*v[1],cd=v[2]*v[3];
    if(!alpha && (outs&8192u)!=0u)ab=vec3(ab.x+ab.y+ab.z);
    if(!alpha && (outs&4096u)!=0u)cd=vec3(cd.x+cd.y+cd.z);
    bool select=(control&256u)!=0u?r[12].a>=0.5:(int(clamp(r[12].a,0.0,1.0)*255.0)&1)!=0;
    ivec3 dest=ivec3((outs>>4u)&15u,outs&15u,(outs>>8u)&15u);
    for(int k=0;k<(alpha?1:3);k++) {
        vec3 values=vec3(ab[k],cd[k],(outs&16384u)!=0u?(select?cd[k]:ab[k]):ab[k]+cd[k]);
        for(int j=0;j<3;j++)if(dest[j]!=0)next[dest[j]][alpha?3:k]=outputValue(values[j],outs);
    }
    if(!alpha) {
        if((outs&524288u)!=0u && dest.x!=0)next[dest.x].a=outputValue(ab.z,outs);
        if((outs&262144u)!=0u && dest.y!=0)next[dest.y].a=outputValue(cd.z,outs);
    }
}
vec4 sampleStage(int i) {
    if(texMode[i]==0)return vec4(0,0,0,1);
    if(texMode[i]==4)return texcoord[i];
    if(texMode[i]!=1 || texValid[i]==0 || texcoord[i].w==0)return vec4(0,0,0,1);
    vec2 uv=texcoord[i].xy/texcoord[i].w;
    if(texNormalized[i]==0)uv/=texSize[i];
    if(i==0)return texture(tex0,uv);
    if(i==1)return texture(tex1,uv);
    if(i==2)return texture(tex2,uv);
    return texture(tex3,uv);
}
bool compareAlpha(float a,float b) {
    int f=alphaFunc&7;
    if(f==0)return false;
    if(f==1)return a<b;
    if(f==2)return a==b;
    if(f==3)return a<=b;
    if(f==4)return a>b;
    if(f==5)return a!=b;
    if(f==6)return a>=b;
    return true;
}
void main() {
    // Host framebuffer rows map directly to guest rows, without a Y flip.
    ivec2 pixel=ivec2(floor(gl_FragCoord.xy));
    bool insideWindow=false;
    for(int i=0;i<8;i++) {
        ivec4 r=windowRect[i];
        insideWindow=insideWindow || (all(greaterThanEqual(pixel,r.xy)) &&
                                     all(lessThanEqual(pixel,r.zw)));
    }
    if(insideWindow == (windowExclusive != 0)) discard;
    vec4 r[16],next[16];
    for(int i=0;i<16;i++)r[i]=vec4(0);
    r[4]=diffuse;r[5]=specular;
    for(int i=0;i<4;i++)r[8+i]=sampleStage(i);
    r[12].a=r[8].a;
    uint count=min(control&15u,8u);
    vec4 result;
    if(count==0u)result=legacyTextured!=0?r[8]:diffuse;
    else {
        for(uint i=0u;i<count;i++) {
            r[1]=constant0[(control&4096u)!=0u?i:0u];
            r[2]=constant1[(control&65536u)!=0u?i:0u];
            for(int j=0;j<16;j++)next[j]=r[j];
            halfStage(r,next,combiner[i].x,combiner[i].z,false);
            halfStage(r,next,combiner[i].y,combiner[i].w,true);
            for(int j=0;j<16;j++)r[j]=next[j];
        }
        if((finalInput.x|finalInput.y)==0u)result=r[12];
        else {
            r[1]=final0;r[2]=final1;
            for(int k=0;k<3;k++) {
                r[14][k]=((finalInput.y&64u)!=0u?1.0-r[5][k]:r[5][k])+((finalInput.y&32u)!=0u?1.0-r[12][k]:r[12][k]);
                if((finalInput.y&128u)!=0u)r[14][k]=clamp(r[14][k],0.0,1.0);
                r[15][k]=inputValue(r,finalInput.y>>24u,k,false)*inputValue(r,finalInput.y>>16u,k,false);
            }
            for(int k=0;k<3;k++) {
                float a=inputValue(r,finalInput.x>>24u,k,false),b=inputValue(r,finalInput.x>>16u,k,false);
                float c=inputValue(r,finalInput.x>>8u,k,false),d=inputValue(r,finalInput.x,k,false);
                result[k]=a*b+(1.0-a)*c+d;
            }
            result.a=inputValue(r,finalInput.y>>8u,0,true);
        }
    }
    result=floor(clamp(result,0.0,1.0)*255.0+0.5)/255.0;
    if(alphaEnable!=0 && !compareAlpha(floor(result.a*255.0+0.5),float(alphaRef)))discard;
    color=result;
}
