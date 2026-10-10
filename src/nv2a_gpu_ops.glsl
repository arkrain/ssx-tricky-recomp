// Experimental arithmetic for differential GPU vertex testing only.
// Semantics follow third_party/nv2a_vsh_cpu/src/nv2a_vsh_cpu.c.
bool finite_value(float x) { return !isnan(x) && !isinf(x); }
float capped(float x) {
    return isinf(x) ? uintBitsToFloat((floatBitsToUint(x)&0xff000000u)+0x7fffffu) : x;
}
// Reconstruct tiny float values through their bits: GPU arithmetic can flush them.
// Pack powers of two exactly; the tested driver rounds the decimal 2^-149
// literal one double ULP downward, which changes float halfway products.
precise double widen(float x) {
    uint b=floatBitsToUint(x);
    if((b&0x7f800000u)==0u && (b&0x7fffffu)!=0u) {
        double v=double(b&0x7fffffu)*packDouble2x32(uvec2(0u,874u<<20));
        return (b&0x80000000u)!=0u?-v:v;
    }
    return double(x);
}
precise float narrow(double x) {
    if(abs(x)<packDouble2x32(uvec2(0u,897u<<20))) {
        uint sign=unpackDouble2x32(x).y&0x80000000u;
        uint mantissa=uint(roundEven(abs(x)*packDouble2x32(uvec2(0u,1172u<<20))));
        return uintBitsToFloat(sign|mantissa);
    }
    return float(x);
}
precise float sum_values(float a,float b) {return narrow(widen(a)+widen(b));}
float signed_input(float x,bool negative) {
    uint b=floatBitsToUint(x);
    // The interpreter multiplies by +/-1: NaNs are quieted without flipping
    // their sign. Unary sign-bit negation has different NaN semantics.
    if((b&0x7f800000u)==0x7f800000u && (b&0x7fffffu)!=0u)
        return uintBitsToFloat(b|0x00400000u);
    return uintBitsToFloat(b^(negative?0x80000000u:0u));
}
precise float product(float a,float b) {
    precise float x=narrow(widen(a)*widen(b));
    return isinf(a)||isinf(b) ? x : capped(x);
}
float less_value(float a,float b) {
    return widen(a)<widen(b) || (floatBitsToUint(a)==0x80000000u && floatBitsToUint(b)==0u) ? 1.0 : 0.0;
}
void ssx_native_op_mov(out float o[4],float p[12]) {
    for(int k=0;k<4;k++)o[k]=p[k];
}
void ssx_native_op_mul(out float o[4],float p[12]) {
    for(int k=0;k<4;k++)o[k]=product(p[k],p[k+4]);
}
void ssx_native_op_add(out float o[4],float p[12]) {
    for(int k=0;k<4;k++){precise float x=sum_values(p[k],p[k+4]);o[k]=x;}
}
void ssx_native_op_mad(out float o[4],float p[12]) {
    for(int k=0;k<4;k++){precise float x=sum_values(product(p[k],p[k+4]),p[k+8]);o[k]=x;}
}
void ssx_native_op_dp3(out float o[4],float p[12]) {
    precise float x=sum_values(product(p[0],p[4]),product(p[1],p[5]));
    x=sum_values(x,product(p[2],p[6]));x=capped(x);
    for(int k=0;k<4;k++)o[k]=x;
}
void ssx_native_op_dp4(out float o[4],float p[12]) {
    precise float x=sum_values(product(p[0],p[4]),product(p[1],p[5]));
    x=sum_values(x,product(p[2],p[6]));x=sum_values(x,product(p[3],p[7]));x=capped(x);
    for(int k=0;k<4;k++)o[k]=x;
}
void ssx_native_op_min(out float o[4],float p[12]) {
    for(int k=0;k<4;k++)o[k]=widen(p[k])<widen(p[k+4])?p[k]:p[k+4];
}
void ssx_native_op_max(out float o[4],float p[12]) {
    for(int k=0;k<4;k++)o[k]=widen(p[k])>widen(p[k+4])?p[k]:p[k+4];
}
void ssx_native_op_sge(out float o[4],float p[12]) {
    for(int k=0;k<4;k++){precise float x=-1.0*less_value(p[k],p[k+4])+1.0;o[k]=x;}
}
precise float reciprocal(float a) {
    if(a==1.0)return 1.0;
    if((floatBitsToUint(a)&0x7fffffffu)==0u)return uintBitsToFloat((floatBitsToUint(a)&0x80000000u)|0x7f800000u);
    return narrow(double(1.0)/widen(a));
}
void ssx_native_op_rcp(out float o[4],float p[12]) {
    precise float x=reciprocal(p[0]);
    for(int k=0;k<4;k++)o[k]=x;
}
void ssx_native_op_rcc(out float o[4],float p[12]) {
    precise double q=double(1.0)/widen(p[0]);
    precise float x=p[0]==1.0?1.0:narrow(q);
    if(p[0]!=1.0) {
        // Preserve the sign of tiny positive reciprocals before GPU FTZ
        // turns their comparison with zero into the negative-clamp branch.
        if(q>double(0.0) && x==0.0)x=5.42101e-20;
        if(x>0.0) {
            if(x<5.42101e-20)x=5.42101e-20;
            else if(x>1.884467e19)x=uintBitsToFloat(0x5f800000u);
        } else {
            if(x < -1.884467e19)x=uintBitsToFloat(0xdf800000u);
            else if(x > -5.42101e-20)x=-5.42101e-20;
        }
    }
    for(int k=0;k<4;k++)o[k]=x;
}
void ssx_native_op_rsq(out float o[4],float p[12]) {
    precise float a=uintBitsToFloat(floatBitsToUint(p[0])&0x7fffffffu);
    precise float root=narrow(sqrt(widen(a)));
    precise float x=reciprocal(root);
    for(int k=0;k<4;k++)o[k]=x;
}
precise float exponent_two(float a) {
    if(isnan(a))return a;
    if(a>=128.0)return uintBitsToFloat(0x7f800000u);
    if(a<=-150.0)return 0.0;
    int n=int(floor(widen(a)+double(0.5)));
    precise double y=(widen(a)-double(n))*0.693147180559945309417232121458176568lf;
    // exp(y), |y| <= ln(2)/2. Degree13 keeps truncation below float rounding.
    precise double term=double(1.0),sum=double(1.0);
    for(int k=1;k<=13;k++){term=(term*y)/double(k);sum=sum+term;}
    precise double scale=packDouble2x32(uvec2(0u,uint(n+1023)<<20));
    return narrow(sum*scale);
}
void ssx_native_op_exp(out float o[4],float p[12]) {
    precise float x=narrow(floor(widen(p[0])));
    o[0]=exponent_two(x);o[1]=sum_values(p[0],signed_input(x,true));o[2]=exponent_two(p[0]);o[3]=1.0;
}
