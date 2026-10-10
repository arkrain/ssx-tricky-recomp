/* Offline GPU vertex experiment. Never selected by the gameplay renderer. */
#include "nv2a_gl.h"
#include "nv2a_vertex.h"
#include <epoxy/gl.h>
#include <assert.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>
#include <errno.h>
int reference_vertex_method(uint32_t,uint32_t);
int reference_vertex_execute(const float [16][4],float [13][4]);
#ifdef SSX_PROBE_TRACE
int probe_trace(unsigned,const float*,const float*,float*);
#endif
static uint32_t rng=0x535358;
static int wide_values;
static float random_value(void){
    rng=rng*1664525u+1013904223u;
    if(wide_values){
        uint32_t b=rng;
        if((b&0x7f800000u)==0x7f800000u)b^=0x00800000u;
        float value;memcpy(&value,&b,4);return value;
    }
    return ((int)(rng>>16)-32768)/1024.f;
}
static size_t load(const char *dir,unsigned program,const char *suffix,void *out,size_t cap){
    char name[4096];snprintf(name,sizeof name,"%s/program%u%s",dir,program,suffix);
    FILE *f=fopen(name,"rb");if(!f){perror(name);exit(2);}
    size_t n=fread(out,1,cap,f);assert(!ferror(f)&&fgetc(f)==EOF);fclose(f);return n;
}
static void method(uint32_t address,uint32_t value){
    reference_vertex_method(address,value);ssx_vertex_method(address,value);
}
static void constant(unsigned index,float value){
    uint32_t bits;memcpy(&bits,&value,4);method(0xb80+(index%4)*4,bits);
}
static uint32_t bits(float x){uint32_t b;memcpy(&b,&x,4);return b;}
static uint32_t ordered(float x){uint32_t b=bits(x);return b&0x80000000u?~b:b|0x80000000u;}
/* Additional diagnostic of values consumed by the current renderer. Raw
 * comparison and exit status remain strict; this is not a pixel comparison. */
static int renderer_equal(int cpu_ok,const float *cpu,const float *gpu,int *rejected){
    int cv=cpu_ok,gv=gpu[52]==1;
    for(unsigned k=0;k<4;k++){cv&=isfinite(cpu[k]);gv&=isfinite(gpu[k]);}
    *rejected=!cv && !gv;
    if(!cv || !gv)return cv==gv;
    for(unsigned k=0;k<4;k++)if(bits(cpu[k])!=bits(gpu[k]))return 0;
    for(unsigned k=12;k<20;k++){
        float a=cpu[k]<0?0:cpu[k]<1?cpu[k]:1;
        float b=gpu[k]<0?0:gpu[k]<1?gpu[k]:1;
        if(bits(a)!=bits(b))return 0;
    }
    for(unsigned k=36;k<52;k++)if(bits(cpu[k])!=bits(gpu[k]))return 0;
    return 1;
}
static GLuint compile(const char *dir,unsigned number){
    char *source=calloc(262144,1);assert(source);
    size_t n=load(dir,number,".vert",source,262143);assert(n);
    GLuint vs=glCreateShader(GL_VERTEX_SHADER);const char *s=source;
    glShaderSource(vs,1,&s,NULL);glCompileShader(vs);free(source);
    GLint ok;glGetShaderiv(vs,GL_COMPILE_STATUS,&ok);
    if(!ok){char log[8192];glGetShaderInfoLog(vs,sizeof log,NULL,log);fprintf(stderr,"program%u: %s\n",number,log);exit(2);}
    GLuint p=glCreateProgram();glAttachShader(p,vs);
    const char *varyings[]={"captured","valid_result"};
    glTransformFeedbackVaryings(p,2,varyings,GL_INTERLEAVED_ATTRIBS);
    glLinkProgram(p);glDeleteShader(vs);glGetProgramiv(p,GL_LINK_STATUS,&ok);
    if(!ok){char log[8192];glGetProgramInfoLog(p,sizeof log,NULL,log);fprintf(stderr,"program%u link: %s\n",number,log);exit(2);}
    return p;
}
static void dump_case(unsigned program,unsigned trial,const uint32_t *code,size_t bytes,
                      const float *input,const float *context,const float *expected,const float *actual){
    const char *root=getenv("SSX_PROBE_DIFFERENCE_DIR");if(!root)return;
    char directory[4096],name[4096];
    assert(snprintf(directory,sizeof directory,"%s/vertex-%u-%u",root,program,trial)<sizeof directory);
    if(mkdir(directory,0700) && errno!=EEXIST){perror(directory);exit(2);}
    const char *suffixes[]={"-code.bin","-input.bin","-constants.bin","-expected.bin","-actual.bin"};
    const void *data[]={code,input,context,expected,actual};size_t sizes[]={bytes,256,3072,208,212};
    for(unsigned i=0;i<5;i++){
        assert(snprintf(name,sizeof name,"%s/program%u%s",directory,program,suffixes[i])<sizeof name);
        FILE *f=fopen(name,"wb");if(!f){perror(name);exit(2);}
        assert(fwrite(data[i],1,sizes[i],f)==sizes[i]);assert(!fclose(f));
    }
}
static double seconds(void){
    struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;
}
static int compare_double(const void *a,const void *b){
    double x=*(const double*)a,y=*(const double*)b;return (x>y)-(x<y);
}
static void benchmark(unsigned program,const float initial[64],GLuint input_buffer,
                      GLuint output_buffer,unsigned count){
    float *inputs=malloc(count*64*sizeof(float)),*gpu=malloc(count*53*sizeof(float));
    float *cpu=malloc(count*53*sizeof(float));assert(inputs&&gpu&&cpu);
    for(unsigned i=0;i<count;i++){
        memcpy(inputs+i*64,initial,64*sizeof(float));inputs[i*64]+=(i%257)*.0009765625f;
    }
    glBindBuffer(GL_ARRAY_BUFFER,input_buffer);
    glBufferData(GL_ARRAY_BUFFER,count*64*sizeof(float),inputs,GL_STREAM_DRAW);
    glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER,output_buffer);
    glBufferData(GL_TRANSFORM_FEEDBACK_BUFFER,count*53*sizeof(float),NULL,GL_STREAM_READ);
    GLuint query;glGenQueries(1,&query);
    double cpu_times[31],gpu_times[31],transfer_times[31];
    for(unsigned repetition=0;repetition<34;repetition++){
        double start=seconds();
        for(unsigned i=0;i<count;i++)cpu[i*53+52]=ssx_vertex_execute(
            (const float (*)[4])(inputs+i*64),(float (*)[4])(cpu+i*53));
        double cpu_time=seconds()-start;
        glBeginQuery(GL_TIME_ELAPSED,query);
        glBeginTransformFeedback(GL_POINTS);glDrawArrays(GL_POINTS,0,count);glEndTransformFeedback();
        glEndQuery(GL_TIME_ELAPSED);
        GLuint64 ns;glGetQueryObjectui64v(query,GL_QUERY_RESULT,&ns);
        start=seconds();
        glBufferSubData(GL_ARRAY_BUFFER,0,count*64*sizeof(float),inputs);
        glBeginTransformFeedback(GL_POINTS);glDrawArrays(GL_POINTS,0,count);glEndTransformFeedback();
        glGetBufferSubData(GL_TRANSFORM_FEEDBACK_BUFFER,0,count*53*sizeof(float),gpu);
        double transfer_time=seconds()-start;
        if(repetition>=3){unsigned k=repetition-3;cpu_times[k]=cpu_time;
            gpu_times[k]=ns*1e-9;transfer_times[k]=transfer_time;}
    }
    unsigned differing=0;
    for(unsigned i=0;i<count;i++){
        float expected[13][4];int ok=reference_vertex_execute(
            (const float (*)[4])(inputs+i*64),expected);
        if(memcmp(expected,cpu+i*53,sizeof expected)||memcmp(expected,gpu+i*53,sizeof expected)
            ||cpu[i*53+52]!=ok||gpu[i*53+52]!=ok)differing++;
    }
    assert(glGetError()==GL_NO_ERROR);
    qsort(cpu_times,31,sizeof(double),compare_double);qsort(gpu_times,31,sizeof(double),compare_double);
    qsort(transfer_times,31,sizeof(double),compare_double);
    printf("{\"benchmark_program\":%u,\"vertices\":%u,\"repetitions\":31,"
           "\"cpu_median_seconds\":%.9g,\"gpu_resident_median_seconds\":%.9g,"
           "\"gpu_transfer_median_seconds\":%.9g,\"different_vertices\":%u}\n",
           program,count,cpu_times[15],gpu_times[15],transfer_times[15],differing);
    fflush(stdout);glDeleteQueries(1,&query);free(inputs);free(gpu);free(cpu);
    if(differing)exit(1);
}
int main(int argc,char **argv){
    if(argc!=2 && argc!=4){fprintf(stderr,"Usage: %s GLSL_DIR [PROGRAM CAPTURE_DIR]\n",argv[0]);return 2;}
    setenv("SSX_GL","1",1);if(!ssx_gl_enabled())return 77;
    fprintf(stderr,"[PROBE] %s / %s\n",glGetString(GL_RENDERER),glGetString(GL_VERSION));
    if(!epoxy_has_gl_extension("GL_ARB_gpu_shader5") ||
       !epoxy_has_gl_extension("GL_ARB_gpu_shader_fp64"))return 77;
    GLint capacity;glGetIntegerv(GL_MAX_TRANSFORM_FEEDBACK_INTERLEAVED_COMPONENTS,&capacity);assert(capacity>=53);
    GLuint vao,input_buffer,output_buffer,fbo,rb;
    glGenVertexArrays(1,&vao);glBindVertexArray(vao);
    glGenBuffers(1,&input_buffer);glBindBuffer(GL_ARRAY_BUFFER,input_buffer);
    glBufferData(GL_ARRAY_BUFFER,64*4,NULL,GL_STREAM_DRAW);
    for(unsigned i=0;i<16;i++){glEnableVertexAttribArray(i);glVertexAttribPointer(i,4,GL_FLOAT,GL_FALSE,64*4,(void*)(uintptr_t)(i*16));}
    glGenBuffers(1,&output_buffer);glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER,output_buffer);
    glBufferData(GL_TRANSFORM_FEEDBACK_BUFFER,53*4,NULL,GL_STREAM_READ);
    glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER,0,output_buffer);
    glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    glGenRenderbuffers(1,&rb);glBindRenderbuffer(GL_RENDERBUFFER,rb);
    glRenderbufferStorage(GL_RENDERBUFFER,GL_RGBA8,1,1);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,rb);
    assert(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
    glEnable(GL_RASTERIZER_DISCARD);
    unsigned first=argc==4?strtoul(argv[2],NULL,10):0,last=argc==4?first+1:22;
    unsigned total_differences=0;
    const char *trials_env=getenv("SSX_PROBE_TRIALS"),*seed_env=getenv("SSX_PROBE_SEED");
    unsigned trials=trials_env?strtoul(trials_env,NULL,0):(argc==4?1024:128);
    if(!trials || trials>1000000)return 2;
    if(seed_env)rng=strtoul(seed_env,NULL,0);
    wide_values=getenv("SSX_PROBE_WIDE")!=NULL;
    fprintf(stderr,"[PROBE] trials=%u seed=%u wide=%d\n",trials,rng,wide_values);
    for(unsigned number=first;number<last;number++){
        GLuint program=compile(argv[1],number);glUseProgram(program);
        GLint context_location=glGetUniformLocation(program,"context_bank");
        uint32_t code[136][4];size_t bytes=load(argv[1],number,"-code.bin",code,sizeof code);assert(bytes && !(bytes%16));
        method(0x1e94,6);method(0x1e9c,0);
        for(unsigned i=0;i<bytes/16;i++)for(unsigned k=0;k<4;k++)method(0xb00+k*4,code[i][k]);
        method(0x1ea0,0);
        float context[768],input[64],initial[64];
        if(argc==4){
            uint32_t captured_code[136][4];size_t n=load(argv[3],number,"-code.bin",captured_code,sizeof captured_code);
            assert(n==bytes && !memcmp(code,captured_code,n));
            assert(load(argv[3],number,"-input.bin",initial,sizeof initial)==sizeof initial);
            assert(load(argv[3],number,"-constants.bin",context,sizeof context)==sizeof context);
        }
        unsigned different=0,lanes=0,status=0,position=0,nonfinite=0,examples=0,finite_examples=0;
        double maximum_error=0,maximum_relative_error=0;uint32_t maximum_ulp=0;
        unsigned lane_counts[52]={0},finite_different=0;
        unsigned nan_pairs=0,nonfinite_other=0,finite_output_cases=0,finite_output_different=0;
        unsigned renderer_different=0,renderer_rejected=0;
        for(unsigned trial=0;trial<trials;trial++){
            if(argc==4){memcpy(input,initial,sizeof input);input[0]+=(trial%257)*.0009765625f;}
            else {
                for(unsigned i=0;i<64;i++)input[i]=random_value();
                if(trial%16==0)for(unsigned i=0;i<768;i++)context[i]=random_value();
                if(trial%16==1)input[0]=NAN;
                if(trial%16==2)input[32]=INFINITY;
                if(trial%16==3)input[1]=3.402823466e38f;
                if(trial%16==4)context[0]=NAN;
                if(trial%16==5)context[0]=1;
            }
            method(0x1ea4,0);
            for(unsigned i=0;i<768;i++)constant(i,context[i]);
            glUniform4fv(context_location,192,context);
            float expected[13][4],actual[53];memset(expected,0x5a,sizeof expected);
            #ifdef SSX_PROBE_TRACE
            int ok=probe_trace(number,input,context,&expected[0][0]);
            ok=ok && isfinite(expected[0][0]) && isfinite(expected[0][1]);
#else
            int ok=reference_vertex_execute((const float (*)[4])input,expected);
#endif
            glBindBuffer(GL_ARRAY_BUFFER,input_buffer);glBufferSubData(GL_ARRAY_BUFFER,0,sizeof input,input);
            glBeginTransformFeedback(GL_POINTS);glDrawArrays(GL_POINTS,0,1);glEndTransformFeedback();
            glGetBufferSubData(GL_TRANSFORM_FEEDBACK_BUFFER,0,sizeof actual,actual);
            GLenum error=glGetError();if(error){fprintf(stderr,"GL error %x program%u trial%u\n",error,number,trial);return 2;}
            #ifndef SSX_PROBE_TRACE
            int rejected;renderer_different+=!renderer_equal(ok,&expected[0][0],actual,&rejected);
            renderer_rejected+=rejected;
#endif
            unsigned changed=ok!=(actual[52]==1);if(changed)status++;
            int outputs_finite=1;
            for(unsigned k=0;k<52;k++){
                float e=((float*)expected)[k],v=actual[k];
                outputs_finite &= isfinite(e) && isfinite(v);
                if(bits(e)==bits(v))continue;
                changed++;lanes++;lane_counts[k]++;if(k<4)position++;
                if(!isfinite(e)||!isfinite(v)){
                    nonfinite++;
                    if(isnan(e)&&isnan(v))nan_pairs++;else nonfinite_other++;
                }
                else {
                    if(finite_examples++<6)fprintf(stderr,"[FINITE] program%u trial%u lane%u cpu=%a/%08x gpu=%a/%08x\n",number,trial,k,e,bits(e),v,bits(v));
                    double d=fabs((double)e-v);if(d>maximum_error)maximum_error=d;
                    double relative=d/fmax(1,fabs(e));if(relative>maximum_relative_error)maximum_relative_error=relative;
                    uint32_t x=ordered(e),y=ordered(v),ulp=x>y?x-y:y-x;
                    if(ulp>maximum_ulp)maximum_ulp=ulp;
                }
                if(examples++<6)fprintf(stderr,"[DIFF] program%u trial%u lane%u cpu=%a/%08x gpu=%a/%08x\n",number,trial,k,e,bits(e),v,bits(v));
            }
            finite_output_cases+=!!outputs_finite;
            finite_output_different+=outputs_finite && changed;
            if(outputs_finite && changed)dump_case(number,trial,&code[0][0],bytes,input,context,&expected[0][0],actual);
            if(changed){
                different++;int finite=1;
                for(unsigned k=0;k<64;k++)finite&=isfinite(input[k]);
                for(unsigned k=0;k<768;k++)finite&=isfinite(context[k]);
                finite_different+=!!finite;
            }
        }
        printf("{\"program\":%u,\"cases\":%u,\"different_cases\":%u,\"finite_input_different_cases\":%u,\"different_lanes\":%u,\"position_lanes\":%u,\"nonfinite_lanes\":%u,\"status_differences\":%u,\"max_finite_absolute_error\":%.17g,\"max_relative_error\":%.17g,\"max_ulp\":%u,\"lane_counts\":[",
               number,trials,different,finite_different,lanes,position,nonfinite,status,maximum_error,maximum_relative_error,maximum_ulp);
        for(unsigned k=0;k<52;k++)printf("%s%u",k?",":"",lane_counts[k]);
        printf("],\"nan_payload_lanes\":%u,\"other_nonfinite_lanes\":%u,"
               "\"finite_output_cases\":%u,\"finite_output_different_cases\":%u,"
               "\"renderer_different_cases\":%u,\"renderer_rejected_cases\":%u}\n",
               nan_pairs,nonfinite_other,finite_output_cases,finite_output_different,renderer_different,renderer_rejected);
        fflush(stdout);
        total_differences+=different;
        if(argc==4 && !different && getenv("SSX_PROBE_BENCH")){
            benchmark(number,initial,input_buffer,output_buffer,256);
            benchmark(number,initial,input_buffer,output_buffer,4096);
        }
        glDeleteProgram(program);
    }
    glDeleteBuffers(1,&input_buffer);glDeleteBuffers(1,&output_buffer);
    glDeleteVertexArrays(1,&vao);glDeleteRenderbuffers(1,&rb);glDeleteFramebuffers(1,&fbo);
    return total_differences?1:0;
}
