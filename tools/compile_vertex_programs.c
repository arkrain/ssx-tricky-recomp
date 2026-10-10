/* Compile bounded vertex programs embedded in the user's XBE into C.
 * The generated file contains original game data and belongs in build/, not
 * source control. Exact runtime word matching preserves the interpreter for
 * modified programs and programs this compiler deliberately does not handle. */
#include "nv2a_vsh_disassembler.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *names[]={"nop","mov","mul","add","mad","dp3","dph","dp4","dst",
    "min","max","slt","sge","arl","rcp","rcc","rsq","exp","log","lit"};
static unsigned u32(const unsigned char *p){return p[0]|(unsigned)p[1]<<8|(unsigned)p[2]<<16|(unsigned)p[3]<<24;}
static int supported(const Nv2aVshOperation *op) {
    if(op->opcode==NV2AOP_NOP)return 1;
    if(op->opcode>NV2AOP_LIT)return 0;
    for(unsigned i=0;i<3;i++) {
        const Nv2aVshInput *in=&op->inputs[i];
        if(in->type==NV2ART_NONE)continue;
        if(!((in->type==NV2ART_INPUT && in->index<16) ||
             (in->type==NV2ART_TEMPORARY && in->index<=12) ||
             (in->type==NV2ART_CONTEXT && in->index<192)))return 0;
        for(unsigned j=0;j<4;j++)if(in->swizzle[j]>3)return 0;
    }
    for(unsigned i=0;i<2;i++) {
        const Nv2aVshOutput *out=&op->outputs[i];
        if(!((out->type==NV2ART_NONE) ||
             (out->type==NV2ART_OUTPUT && out->index<13) ||
             (out->type==NV2ART_TEMPORARY && out->index<12) ||
             (out->type==NV2ART_ADDRESS && out->index==0)))return 0;
    }
    return 1;
}
static void emit_inputs(FILE *f,const Nv2aVshOperation *op,const char *name) {
    if(!op->opcode)return;
    fprintf(f,"float %s[12]={0};\n",name);
    for(unsigned i=0;i<3;i++) {
        const Nv2aVshInput *in=&op->inputs[i];
        if(in->type==NV2ART_NONE)break;
        const char *bank=in->type==NV2ART_INPUT?"v":in->type==NV2ART_CONTEXT?"c":in->index==12?"o":"r";
        unsigned index=in->type==NV2ART_TEMPORARY && in->index==12?0:in->index;
        if(in->type==NV2ART_CONTEXT && in->is_relative) {
            fprintf(f,"if(!isfinite(a) || a < -192 || a > 192 || %u+(int)a<0 || %u+(int)a>=192)return 0;\n",index,index);
        }
        for(unsigned j=0;j<4;j++) {
            fprintf(f,"%s[%u]=%s1.0f*%s[",name,i*4+j,in->is_negated?"-":"",bank);
            if(in->type==NV2ART_CONTEXT && in->is_relative)fprintf(f,"(%u+(int)a)*4+%u",index,in->swizzle[j]);
            else fprintf(f,"%u",index*4+in->swizzle[j]);
            fprintf(f,"];\n");
        }
    }
}
static void emit_operation(FILE *f,const Nv2aVshOperation *op,const char *inputs) {
    if(!op->opcode)return;
    fprintf(f,"ssx_native_op_%s(result,%s);\n",names[op->opcode],inputs);
    for(unsigned i=0;i<2;i++) {
        const Nv2aVshOutput *out=&op->outputs[i];
        if(out->type==NV2ART_NONE)continue;
        unsigned mask=out->writemask;
        if(op->opcode==NV2AOP_ARL && out->type==NV2ART_ADDRESS)mask=8;
        for(unsigned j=0;j<4;j++)if(mask&(8>>j)) {
            if(out->type==NV2ART_ADDRESS)fprintf(f,"a=result[%u];\n",j);
            else fprintf(f,"%s[%u]=result[%u];\n",out->type==NV2ART_OUTPUT?"o":"r",out->index*4+j,j);
        }
    }
}
int main(int argc,char **argv) {
    if(argc!=3){fprintf(stderr,"Usage: compile_vertex_programs INPUT.xbe OUTPUT.inc\n");return 1;}
    FILE *in=fopen(argv[1],"rb");if(!in){perror(argv[1]);return 1;}
    if(fseek(in,0,SEEK_END)){fclose(in);return 1;}
    long length=ftell(in);rewind(in);
    if(length<4 || length>64*1024*1024){fclose(in);return 1;}
    unsigned char *data=malloc((size_t)length);if(!data){fclose(in);return 1;}
    if(fread(data,1,(size_t)length,in)!=(size_t)length || memcmp(data,"XBEH",4)){fclose(in);free(data);return 1;}
    fclose(in);FILE *f=fopen(argv[2],"w");if(!f){perror(argv[2]);free(data);return 1;}
    fprintf(f,"/* Generated from the user's executable; do not distribute original game data. */\n");
    unsigned count=0,lengths[512];
    for(size_t offset=0;offset+4<=(size_t)length;offset+=4) {
        unsigned header=u32(data+offset),n=header>>16;
        if((header&65535)!=0x2078 || !n || n>136 || offset+4+n*16>(size_t)length)continue;
        uint32_t code[136][4];Nv2aVshStep steps[136];int ok=1;
        for(unsigned i=0;i<n;i++) {
            for(unsigned j=0;j<4;j++)code[i][j]=u32(data+offset+4+i*16+j*4);
            if(code[i][0] || nv2a_vsh_parse_step(&steps[i],code[i])!=NV2AVPR_SUCCESS ||
               !!steps[i].is_final!=(i==n-1) || !supported(&steps[i].mac) || !supported(&steps[i].ilu)){ok=0;break;}
        }
        if(!ok)continue;
        if(count>=512){fclose(f);free(data);return 1;}
        lengths[count]=n;
        fprintf(f,"/* XBE file offset 0x%zx */\nstatic const uint32_t native_code_%u[%u][4]={\n",offset+4,count,n);
        for(unsigned i=0;i<n;i++)fprintf(f,"{0x%x,0x%x,0x%x,0x%x},\n",code[i][0],code[i][1],code[i][2],code[i][3]);
        fprintf(f,"};\nstatic int native_program_%u(const float *v,const float *c,float *o){\nfloat r[48]={0},a=0,result[4];\n",count);
        for(unsigned i=0;i<n;i++) {
            fprintf(f,"{ /* instruction %u: both input sets precede either write */\n",i);
            emit_inputs(f,&steps[i].mac,"m");emit_inputs(f,&steps[i].ilu,"u");
            emit_operation(f,&steps[i].mac,"m");emit_operation(f,&steps[i].ilu,"u");fprintf(f,"}\n");
        }
        fprintf(f,"return 1;}\n");count++;
    }
    fprintf(f,"SsxNativeVertexFn ssx_vertex_native_find(const uint32_t (*w)[4],unsigned available){\n");
    for(unsigned i=0;i<count;i++)fprintf(f,"if(available>=%u && !memcmp(w,native_code_%u,sizeof native_code_%u))return native_program_%u;\n",lengths[i],i,i,i);
    fprintf(f,"return NULL;}\n");
    int failed=ferror(f);if(fclose(f))failed=1;free(data);
    fprintf(stderr,"Compiled %u embedded vertex programs\n",count);
    return failed || !count;
}
