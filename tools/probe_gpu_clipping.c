/* Offline comparison of ordered GPU clipping against the production CPU path. */
#define xbox_GetMemoryOffset clip_probe_memory_offset
#include "../third_party/xboxrecomp/src/kernel/nv2a_pb_exec.c"
#undef xbox_GetMemoryOffset
#include <epoxy/gl.h>
#include <assert.h>
#include <time.h>
static ptrdiff_t probe_memory;
ptrdiff_t clip_probe_memory_offset(void){return probe_memory;}
typedef void (*recomp_func_t)(void);
recomp_func_t recomp_lookup(uint32_t va){(void)va;return NULL;}
extern ptrdiff_t g_xbox_mem_offset;
static int clip_sample(const void *p,int x,int y,uint32_t *out){
    assert(x>=0 && x<16 && y>=0 && y<16);*out=((const uint32_t*)p)[y*16+x];return 1;
}
static uint32_t probe_seed=0x1505a17u;
static uint32_t probe_random(void){uint32_t x=probe_seed;x^=x<<13;x^=x>>17;x^=x<<5;return probe_seed=x;}
static float probe_value(void){return ((int)(probe_random()%8193)-4096)/32.0f;}
static uint32_t probe_bits(float x){uint32_t b;memcpy(&b,&x,4);return b;}
static GLuint compile_clip_stage(GLenum type,const char *source){
    GLuint stage=glCreateShader(type);glShaderSource(stage,1,&source,NULL);glCompileShader(stage);
    GLint ok;glGetShaderiv(stage,GL_COMPILE_STATUS,&ok);
    if(!ok){char log[8192];glGetShaderInfoLog(stage,sizeof log,NULL,log);fprintf(stderr,"%s\n",log);exit(2);}
    return stage;
}
typedef struct {unsigned triangles,vertices,differences;int crossing;double cpu,gpu;} ClipTiming;
static double clip_seconds(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;}
static int clip_compare_double(const void *a,const void *b){double x=*(const double*)a,y=*(const double*)b;return (x>y)-(x<y);}
static ClipTiming clip_benchmark(GLuint program,GLuint vao,GLuint input,GLuint output,GLuint fbo,unsigned triangles,int crossing){
    ClipTiming result={.triangles=triangles,.crossing=crossing};
    SsxGlVertex *inputs=calloc(triangles*3,sizeof(*inputs)),*actual=calloc(triangles*24,sizeof(*actual));assert(inputs&&actual);
    for(unsigned i=0;i<triangles;i++)for(unsigned v=0;v<3;v++){
        SsxGlVertex *x=inputs+i*3+v;
        x->position[0]=(v==0?(crossing?-4:4):v==1?(crossing?20:12):4)+(i%17)*.01f;
        x->position[1]=v==2?(crossing?20:12):4;x->position[2]=8;x->position[3]=(float)(1u<<v);
        for(unsigned k=4;k<28;k++)((float*)x)[k]=(float)((i+v+k)%127)/127;
    }
    s_gpu.clip_x=s_gpu.clip_y=0;s_gpu.clip_w=s_gpu.clip_h=16;s_gpu.pitch=64;
    float far=16;memcpy(&s_render_reg[0x398/4],&far,4);s_render_reg[0x308/4]=0;s_triangle_colors=1;s_gl_collect=1;
    glBindFramebuffer(GL_FRAMEBUFFER,fbo);glUseProgram(program);glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER,input);glBufferData(GL_ARRAY_BUFFER,triangles*3*112,inputs,GL_STREAM_DRAW);
    glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER,output);glBufferData(GL_TRANSFORM_FEEDBACK_BUFFER,triangles*24*112,NULL,GL_STREAM_READ);
    glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER,0,output);glEnable(GL_RASTERIZER_DISCARD);
    glUniform4f(glGetUniformLocation(program,"clip_bounds"),0,0,16,16);
    glUniform1f(glGetUniformLocation(program,"clip_far"),16);glUniform1i(glGetUniformLocation(program,"cull_enable"),0);
    GLuint query;glGenQueries(1,&query);double cpu[31],gpu[31];
    for(unsigned repetition=0;repetition<34;repetition++){
        s_gl_count=0;double before=clip_seconds();
        for(unsigned i=0;i<triangles;i++){
            float position[3][4];
            for(unsigned v=0;v<3;v++){
                const SsxGlVertex *x=inputs+i*3+v;memcpy(position[v],x->position,16);
                memcpy(s_triangle_color[v],x->diffuse,32);memcpy(s_triangle_tex[v],x->tex,64);
            }
            raster_shader_triangle(position,0xffffffff);
        }
        double duration=clip_seconds()-before;
        glBeginQuery(GL_TIME_ELAPSED,query);glBeginTransformFeedback(GL_TRIANGLES);
        glDrawArrays(GL_TRIANGLES,0,triangles*3);glEndTransformFeedback();glEndQuery(GL_TIME_ELAPSED);
        GLuint64 ns;glGetQueryObjectui64v(query,GL_QUERY_RESULT,&ns);
        if(repetition>=3){cpu[repetition-3]=duration;gpu[repetition-3]=ns*1e-9;}
    }
    glDeleteQueries(1,&query);glGenQueries(1,&query);
    glBeginQuery(GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN,query);glBeginTransformFeedback(GL_TRIANGLES);
    glDrawArrays(GL_TRIANGLES,0,triangles*3);glEndTransformFeedback();glEndQuery(GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN);
    GLuint count;glGetQueryObjectuiv(query,GL_QUERY_RESULT,&count);count*=3;assert(count<=triangles*24);
    result.vertices=count;result.differences=count!=s_gl_count;
    glGetBufferSubData(GL_TRANSFORM_FEEDBACK_BUFFER,0,count*112,actual);
    for(unsigned k=0;k<(count<s_gl_count?count:s_gl_count)*28;k++)
        result.differences+=probe_bits(((float*)actual)[k])!=probe_bits(((float*)s_gl_vertices)[k]);
    assert(glGetError()==GL_NO_ERROR);qsort(cpu,31,sizeof(double),clip_compare_double);qsort(gpu,31,sizeof(double),clip_compare_double);
    result.cpu=cpu[15];result.gpu=gpu[15];glDeleteQueries(1,&query);free(inputs);free(actual);return result;
}
int main(int argc,char **argv){
    if(argc!=3){fprintf(stderr,"Usage: %s GEOMETRY_SHADER TRIALS\n",argv[0]);return 2;}
    unsigned trials=strtoul(argv[2],NULL,0);if(!trials || trials>1000000)return 2;
    setenv("SSX_GL","1",1);if(!ssx_gl_enabled())return 77;
    fprintf(stderr,"[CLIP] %s / %s\n",glGetString(GL_RENDERER),glGetString(GL_VERSION));
    if(!epoxy_has_gl_extension("GL_ARB_gpu_shader5") || !epoxy_has_gl_extension("GL_ARB_gpu_shader_fp64"))return 77;
    FILE *file=fopen(argv[1],"rb");assert(file);char *geometry=calloc(262144,1);assert(geometry);
    assert(fread(geometry,1,262143,file)>0 && !ferror(file) && fgetc(file)==EOF);fclose(file);
    const char *vertex="#version 330 core\nlayout(location=0)in vec4 attributes[7];"
        "out VertexData{vec4 values[7];} vertex;void main(){"
        "for(int k=0;k<7;k++)vertex.values[k]=attributes[k];gl_Position=vec4(0,0,0,1);}";
    GLuint vs=compile_clip_stage(GL_VERTEX_SHADER,vertex),gs=compile_clip_stage(GL_GEOMETRY_SHADER,geometry);free(geometry);
    GLuint program=glCreateProgram();glAttachShader(program,vs);glAttachShader(program,gs);
    const char *varying="captured";glTransformFeedbackVaryings(program,1,&varying,GL_INTERLEAVED_ATTRIBS);
    glLinkProgram(program);GLint ok;glGetProgramiv(program,GL_LINK_STATUS,&ok);
    if(!ok){char log[8192];glGetProgramInfoLog(program,sizeof log,NULL,log);fprintf(stderr,"%s\n",log);return 2;}
    glDeleteShader(vs);glDeleteShader(gs);
    GLuint vao,input,output,query,fbo,rb;glGenVertexArrays(1,&vao);glBindVertexArray(vao);
    glGenBuffers(1,&input);glBindBuffer(GL_ARRAY_BUFFER,input);glBufferData(GL_ARRAY_BUFFER,3*112,NULL,GL_STREAM_DRAW);
    for(unsigned k=0;k<7;k++){glEnableVertexAttribArray(k);glVertexAttribPointer(k,4,GL_FLOAT,GL_FALSE,112,(void*)(uintptr_t)(k*16));}
    glGenBuffers(1,&output);glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER,output);
    glBufferData(GL_TRANSFORM_FEEDBACK_BUFFER,24*112,NULL,GL_STREAM_READ);glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER,0,output);
    glGenQueries(1,&query);glGenFramebuffers(1,&fbo);glBindFramebuffer(GL_FRAMEBUFFER,fbo);
    glGenRenderbuffers(1,&rb);glBindRenderbuffer(GL_RENDERBUFFER,rb);glRenderbufferStorage(GL_RENDERBUFFER,GL_RGBA8,1,1);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER,GL_COLOR_ATTACHMENT0,GL_RENDERBUFFER,rb);
    assert(glCheckFramebufferStatus(GL_FRAMEBUFFER)==GL_FRAMEBUFFER_COMPLETE);
    glEnable(GL_RASTERIZER_DISCARD);glUseProgram(program);
    GLint bounds=glGetUniformLocation(program,"clip_bounds"),far_location=glGetUniformLocation(program,"clip_far");
    GLint cull=glGetUniformLocation(program,"cull_enable"),front=glGetUniformLocation(program,"front_clockwise"),face=glGetUniformLocation(program,"cull_face");
    unsigned char *ram=calloc(1,4*1024*1024);assert(ram);probe_memory=g_xbox_mem_offset=(ptrdiff_t)(uintptr_t)ram;
    s_gpu.pitch=64;s_gpu.color_offset=0x300000;s_triangle_colors=1;
    unsigned different=0,count_different=0,lanes=0,nonfinite=0,examples=0;
    const char *pixel_env=getenv("SSX_CLIP_PIXEL_CASES");unsigned pixel_cases=pixel_env?strtoul(pixel_env,NULL,0):0;
    unsigned pixel_different_cases=0,pixel_differences=0,pixel_nonempty=0,pixel_tested=0;
    uint32_t texels[256];for(unsigned k=0;k<256;k++)texels[k]=0xff000000u|((k*73u&255)<<16)|((k*29u&255)<<8)|(k*17u&255);
    unsigned long long cpu_vertices=0,gpu_vertices=0;
    for(unsigned trial=0;trial<trials;trial++){
        s_gpu.clip_x=trial%4;s_gpu.clip_y=trial%7;s_gpu.clip_w=16+trial%23;s_gpu.clip_h=16+trial%31;
        if(trial>=150000){s_gpu.clip_x=s_gpu.clip_y=0;s_gpu.clip_w=s_gpu.clip_h=16;}
        s_gpu.pitch=s_gpu.clip_w*4;
        float far=trial%37==0?NAN:trial%41==0?-1:trial%43==0?INFINITY:16;
        memcpy(&s_render_reg[0x398/4],&far,4);
        unsigned cull_mode=(trial/8)%4;
        s_render_reg[0x308/4]=cull_mode!=0;s_render_reg[0x3a0/4]=(trial/32)%2?0x900:0x901;
        s_render_reg[0x39c/4]=cull_mode==3?0x408:cull_mode==2?0x404:0x405;
        unsigned selection=trial%6;unsigned combiner_input=selection<2?selection+4:selection+6;
        s_render_reg[0x1e60/4]=1;s_render_reg[0x288/4]=combiner_input;
        s_render_reg[0x28c/4]=(combiner_input|16)<<8;s_render_reg[0x1e70/4]=4|(4<<5)|(4<<10)|(4<<15);
        float position[3][4];SsxGlVertex inputs[3],actual[24];
        for(unsigned v=0;v<3;v++){
            for(unsigned k=0;k<3;k++)position[v][k]=probe_value();
            position[v][3]=.25f+(probe_random()%16)*.25f;
            if(trial%8==1)position[v][3]=probe_value();
            if(trial%8==2){position[v][0]=s_gpu.clip_x+(v==0?-1e-6f:v==1?0:1e-6f);position[v][1]=4+v*3;position[v][2]=2;}
            if(trial%8==3){position[v][0]=v==0?3e38f:v==1?-3e38f:8;position[v][1]=v==0?-3e38f:v==1?3e38f:8;position[v][2]=2;position[v][3]=v==2?1:3e38f;}
            if(trial%8==4)position[v][3]=v==0?-1e-5f:v==1?1e-5f:1;
            if(trial%8==5){position[v][0]=v==0?-4:v==1?12:4;position[v][1]=v==2?12:4;position[v][2]=2;position[v][3]=1;}
            if(trial%8==6)position[v][2]=v==0?-1e-6f:v==1?16.000002f:8;
            if(trial%8==7 && v==0)position[v][trial%4]=trial%2?NAN:INFINITY;
            if(trial>=150000){
                float tiny;uint32_t tiny_bits=1;memcpy(&tiny,&tiny_bits,4);
                position[v][0]=v==1?12:4;position[v][1]=v==2?12:4;position[v][2]=8;position[v][3]=1;
                unsigned special=(trial-150000)%4;
                if(v==0){if(special==0)position[v][0]=tiny;
                    if(special==1)position[v][3]=tiny;
                    if(special==2)position[v][2]=-tiny;
                    if(special==3)position[v][3]=-0.0f;}
            }
            float varying[24];for(unsigned k=0;k<24;k++)varying[k]=probe_value();
            if(trial%11==0)varying[(trial+v)%24]=NAN;
            if(trial%13==0)varying[(trial+v*2)%24]=INFINITY;
            if(trial%17==0)varying[(trial+v*3)%24]=-INFINITY;
            if(trial%19==0)varying[(trial+v*5)%24]=-0.0f;
            memcpy(s_triangle_color[v],varying,32);memcpy(s_triangle_tex[v],varying+8,64);
            memcpy(inputs[v].position,position[v],16);memcpy(inputs[v].diffuse,varying,96);
        }
        s_gl_collect=1;s_gl_count=0;raster_shader_triangle(position,0xff123456);
        glBindFramebuffer(GL_FRAMEBUFFER,fbo);glUseProgram(program);glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER,input);glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER,output);
        glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER,0,output);glEnable(GL_RASTERIZER_DISCARD);
        glUniform4f(bounds,s_gpu.clip_x,s_gpu.clip_y,s_gpu.clip_w,s_gpu.clip_h);
        glUniform1f(far_location,isfinite(far)&&far>0?far:16777215.f);
        glUniform1i(cull,s_render_reg[0x308/4]!=0);glUniform1i(front,s_render_reg[0x3a0/4]==0x900);glUniform1i(face,s_render_reg[0x39c/4]);
        glBufferSubData(GL_ARRAY_BUFFER,0,sizeof inputs,inputs);
        glBeginQuery(GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN,query);
        glBeginTransformFeedback(GL_TRIANGLES);glDrawArrays(GL_TRIANGLES,0,3);glEndTransformFeedback();glEndQuery(GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN);
        GLuint primitives;glGetQueryObjectuiv(query,GL_QUERY_RESULT,&primitives);unsigned count=primitives*3;assert(count<=24);
        if(count)glGetBufferSubData(GL_TRANSFORM_FEEDBACK_BUFFER,0,count*112,actual);
        assert(glGetError()==GL_NO_ERROR);cpu_vertices+=s_gl_count;gpu_vertices+=count;
        unsigned changed=count!=s_gl_count;if(changed)count_different++;
        for(unsigned k=0;k<(count<s_gl_count?count:s_gl_count)*28;k++){
            float a=((float*)s_gl_vertices)[k],b=((float*)actual)[k];if(probe_bits(a)==probe_bits(b))continue;
            lanes++;changed=1;if(!isfinite(a)||!isfinite(b))nonfinite++;
            if(examples++<12)fprintf(stderr,"[DIFF] trial%u vertex%u lane%u CPU=%a/%08x GPU=%a/%08x\n",trial,k/28,k%28,a,probe_bits(a),b,probe_bits(b));
        }
        if(changed){
            if(different==0){
                for(unsigned v=0;v<3;v++)fprintf(stderr,"[INPUT] %a %a %a %a\n",position[v][0],position[v][1],position[v][2],position[v][3]);
                for(unsigned v=0;v<count;v++)fprintf(stderr,"[OUTPUT] %a %a %a %a\n",actual[v].position[0],actual[v].position[1],actual[v].position[2],actual[v].position[3]);
                for(unsigned v=0;v<count;v+=3){float *a=actual[v].position,*b=actual[v+1].position,*c=actual[v+2].position;
                    float area=(b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]);fprintf(stderr,"[AREA] %a cull=%x front=%x face=%x\n",area,s_render_reg[0x308/4],s_render_reg[0x3a0/4],s_render_reg[0x39c/4]);}
            }
            different++;if(count!=s_gl_count && examples++<12)fprintf(stderr,"[COUNT] trial%u CPU=%u GPU=%u\n",trial,s_gl_count,count);}
        if(trial<pixel_cases || changed || trial>=150000){
            uint32_t pixels[2][64*64];SsxGlDraw draw={0};draw.pitch=256;draw.width=draw.height=64;
            draw.clip_x=s_gpu.clip_x;draw.clip_y=s_gpu.clip_y;draw.clip_w=s_gpu.clip_w;draw.clip_h=s_gpu.clip_h;
            draw.color_mask=0xffffffff;draw.registers=s_render_reg;
            for(unsigned stage=0;stage<4;stage++)draw.textures[stage]=(SsxGlTexture){
                .source=texels,.source_size=sizeof texels,.width=16,.height=16,.format=0x12,.pitch=64,
                .wrap_u=trial%3+1,.wrap_v=(trial/3)%3+1,.normalized=1,.valid=1,
                .opaque=texels,.sample=clip_sample,.min_filter=trial%2+1,.mag_filter=trial%2+1};
            glDisable(GL_RASTERIZER_DISCARD);
            for(unsigned variant=0;variant<(changed?6u:1u);variant++){
            if(changed){unsigned selected=variant<2?variant+4:variant+6;
                s_render_reg[0x288/4]=selected;s_render_reg[0x28c/4]=(selected|16)<<8;}
            for(unsigned pass=0;pass<2;pass++){
                memset(pixels[pass],0,sizeof pixels[pass]);ssx_gl_invalidate();draw.color=pixels[pass];
                unsigned n=pass?count:s_gl_count;
                if(n)assert(ssx_gl_draw(&draw,pass?actual:s_gl_vertices,n));
                ssx_gl_sync();
            }
            unsigned changed_pixels=0,nonempty=0;
            for(unsigned k=0;k<64*64;k++){
                nonempty|=pixels[0][k]!=0 || pixels[1][k]!=0;
                if(pixels[0][k]!=pixels[1][k]){
                    if(pixel_differences<6)fprintf(stderr,"[PIXEL] trial%u at%u CPU=%08x GPU=%08x\n",trial,k,pixels[0][k],pixels[1][k]);
                    pixel_differences++;changed_pixels++;
                }
            }
            pixel_nonempty+=!!nonempty;pixel_different_cases+=changed_pixels!=0;pixel_tested++;
            }
        }
    }
    printf("{\"cases\":%u,\"different_cases\":%u,\"count_differences\":%u,\"different_lanes\":%u,\"nonfinite_lanes\":%u,\"cpu_vertices\":%llu,\"gpu_vertices\":%llu,\"pixel_cases\":%u,\"nonempty_pixel_cases\":%u,\"pixel_different_cases\":%u,\"pixel_differences\":%u,\"benchmarks\":[",trials,different,count_different,lanes,nonfinite,cpu_vertices,gpu_vertices,pixel_tested,pixel_nonempty,pixel_different_cases,pixel_differences);
    unsigned benchmark_differences=0;
    if(getenv("SSX_CLIP_BENCH"))for(unsigned i=0;i<4;i++){
        ClipTiming timing=clip_benchmark(program,vao,input,output,fbo,i%2?4096:256,i/2);
        benchmark_differences+=timing.differences;
        printf("%s{\"triangles\":%u,\"crossing\":%d,\"vertices\":%u,\"different_lanes_or_count\":%u,\"cpu_median_seconds\":%.9g,\"gpu_resident_median_seconds\":%.9g}",
               i?",":"",timing.triangles,timing.crossing,timing.vertices,timing.differences,timing.cpu,timing.gpu);
    }
    printf("]}\n");
    glDeleteQueries(1,&query);glDeleteBuffers(1,&input);glDeleteBuffers(1,&output);glDeleteVertexArrays(1,&vao);
    glDeleteRenderbuffers(1,&rb);glDeleteFramebuffers(1,&fbo);glDeleteProgram(program);free(ram);free(s_gl_vertices);
    return different||pixel_differences||benchmark_differences?1:0;
}
