// Offline ordered-clipping experiment. Arithmetic helpers are prepended by the runner.
layout(triangles) in;
layout(triangle_strip,max_vertices=24) out;
in VertexData { vec4 values[7]; } vertices[];
out vec4 captured[7];
uniform vec4 clip_bounds;
uniform float clip_far;
uniform int cull_enable,front_clockwise,cull_face;
precise float polygon[560];
float negate_bits(float x){return uintBitsToFloat(floatBitsToUint(x)^0x80000000u);}
bool tiny_input(float a){uint b=floatBitsToUint(a)&0x7fffffffu;return b!=0u && b<0x00800000u;}
precise float clip_mul(float a,float b){
    precise float q=a*b;
    bool underflow=(floatBitsToUint(q)&0x7f800000u)==0u && a!=0.0 && b!=0.0;
    if(tiny_input(a)||tiny_input(b)||underflow||!finite_value(q))return narrow(widen(a)*widen(b));
    return q;
}
precise float clip_sub(float a,float b){
    precise float q=a-b;
    bool underflow=(floatBitsToUint(q)&0x7f800000u)==0u && a!=b;
    if(tiny_input(a)||tiny_input(b)||underflow||!finite_value(q))return sum_values(a,negate_bits(b));
    return q;
}
precise float clip_div(float a,float b){return narrow(widen(a)/widen(b));}
float distance_to_plane(int bank,int vertex,int plane){
    int at=bank*280+vertex*28;
    float w=polygon[at+3];
    if(plane==0)return clip_sub(w,uintBitsToFloat(0x3727c5acu));
    if(plane==5)return polygon[at+2];
    if(plane==6)return clip_sub(clip_mul(clip_far,w),polygon[at+2]);
    int axis=plane<=2?0:1;
    float edge=clip_bounds[axis];
    if((plane&1)==0)edge=sum_values(edge,clip_bounds[axis+2]);
    precise float d=clip_sub(polygon[at+axis],clip_mul(edge,w));
    return (plane&1)==0?negate_bits(d):d;
}
void main(){
    for(int v=0;v<3;v++){
        for(int k=0;k<4;k++)if(!finite_value(vertices[v].values[0][k]))return;
        for(int k=0;k<28;k++)polygon[v*28+k]=vertices[v].values[k/4][k%4];
        for(int k=0;k<3;k++)polygon[v*28+k]=clip_mul(polygon[v*28+k],polygon[v*28+3]);
    }
    int count=3,src=0;
    for(int plane=0;plane<7;plane++){
        int inside=0;
        for(int i=0;i<count;i++)if(widen(distance_to_plane(src,i,plane))>=0.0lf)inside++;
        if(inside==0)return;
        if(inside==count)continue;
        int dst=src^1,n=0;
        for(int i=0;i<count;i++){
            int next=(i+1)%count;
            float da=distance_to_plane(src,i,plane),db=distance_to_plane(src,next,plane);
            bool ia=widen(da)>=0.0lf,ib=widen(db)>=0.0lf;
            if(ia){
                if(n>=10)return;
                for(int k=0;k<28;k++)polygon[dst*280+n*28+k]=polygon[src*280+i*28+k];
                n++;
            }
            if(ia!=ib){
                if(n>=10)return;
                precise double denominator=widen(da)-widen(db);
                precise double t=widen(da)/denominator;
                for(int k=0;k<28;k++){
                    precise double a=widen(polygon[src*280+i*28+k]);
                    precise double delta=widen(polygon[src*280+next*28+k])-a;
                    precise double product=t*delta;
                    precise double value=a+product;
                    polygon[dst*280+n*28+k]=narrow(value);
                }
                n++;
            }
        }
        count=n;src=dst;
        if(count<3)return;
    }
    for(int i=1;i<count-1;i++){
        int indices[3]=int[3](0,i,i+1);
        precise vec4 positions[3];
        for(int v=0;v<3;v++){
            int at=src*280+indices[v]*28;
            for(int k=0;k<3;k++)positions[v][k]=clip_div(polygon[at+k],polygon[at+3]);
            positions[v][3]=polygon[at+3];
        }
        precise float left=clip_mul(clip_sub(positions[1].x,positions[0].x),clip_sub(positions[2].y,positions[0].y));
        precise float right=clip_mul(clip_sub(positions[1].y,positions[0].y),clip_sub(positions[2].x,positions[0].x));
        precise float area=clip_sub(left,right);
        if(!finite_value(area) || widen(area)==0.0lf)continue;
        if(cull_enable!=0){
            bool front=front_clockwise!=0?widen(area)<0.0lf:widen(area)>0.0lf;
            if(cull_face==0x408 || (cull_face==0x404 && front) || (cull_face==0x405 && !front))continue;
        }
        for(int v=0;v<3;v++){
            captured[0]=positions[v];
            for(int j=1;j<7;j++)for(int k=0;k<4;k++)captured[j][k]=polygon[src*280+indices[v]*28+j*4+k];
            gl_Position=vec4(0,0,0,1);EmitVertex();
        }
        EndPrimitive();
    }
}
