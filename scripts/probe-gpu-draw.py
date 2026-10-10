#!/usr/bin/env python3
"""Replay a captured draw through CPU and combined GPU geometry, offline only.

Generated code and captures contain original game data and remain local.
Production sources and build objects are never modified.
"""
import argparse
import hashlib
import importlib.util
import json
import os
import shlex
import struct
import subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--capture',type=Path,required=True)
p.add_argument('--report-prefix',type=Path,required=True)
p.add_argument('--build-dir',type=Path,default=Path('build/linux-release'))
p.add_argument('--output-dir',type=Path,default=Path('build/gpu-draw160'))
p.add_argument('--indexed',action='store_true',help='Reuse indexed input vertices in the GPU prepass')
p.add_argument('--warmup',type=int,default=100)
p.add_argument('--gpu-first',action='store_true',help='Reverse process order for paired timing rounds')
p.add_argument('--runs',type=int,default=1)
p.add_argument('--clear',action='store_true')
p.add_argument('--two-pass',action='store_true',help='Keep geometry in GPU transform feedback, then reuse the original raster vertex shader')
p.add_argument('--feedback',action='store_true',help='Capture window vertices and clip positions; timings include diagnostic readback')
a=p.parse_args()
if a.indexed and not a.two_pass:p.error('--indexed requires --two-pass')
if not 0<=a.warmup<=10000:p.error('warmup must be between0 and10000')
if a.two_pass and a.feedback:p.error('--two-pass and --feedback are separate diagnostics')
if not 1<=a.runs<=1000:p.error('runs must be between1 and1000')
root=Path(__file__).resolve().parents[1];build=a.build_dir.resolve();out=a.output_dir.resolve();capture=a.capture.resolve()
out.mkdir(parents=True,exist_ok=True);a.report_prefix.parent.mkdir(parents=True,exist_ok=True)
subprocess.run(['python3',str(root/'scripts/generate-vertex-glsl-probe.py'),'--native-inc',str(build/'nv2a_native_programs.inc'),'--output-dir',str(out)],check=True)
words=(capture/'words.bin').read_bytes();start=struct.unpack('<I',(capture/'start.bin').read_bytes())[0]
mode=struct.unpack('<I',(capture/'mode.bin').read_bytes())[0]
if mode&3!=2:p.error('capture is not programmable geometry')
programs=[x for x in json.loads((out/'manifest.json').read_text()) if words[start*16:].startswith((out/f'program{x["program"]}-code.bin').read_bytes())]
if len(programs)!=1:p.error('capture must match exactly one generated program')
number=programs[0]['program'];vertex=(out/f'program{number}.vert').read_text()
vertex=vertex.replace('precise out vec4 captured[13];\nout float valid_result;','out VertexData { vec4 values[7]; } vertex;')
begin=vertex.index('    valid_result=')
end=vertex.index('    gl_Position=',begin)
vertex=vertex[:begin]+'''    for(int k=0;k<4;k++)vertex.values[0][k]=valid?o[k]:uintBitsToFloat(0x7fc00000u);
    for(int k=0;k<8;k++){float x=o[12+k];vertex.values[1+k/4][k%4]=x<0.0?0.0:x<1.0?x:1.0;}
    for(int k=0;k<16;k++)vertex.values[3+k/4][k%4]=o[36+k];
'''+vertex[end:]
geometry='#version 330 core\n#extension GL_ARB_gpu_shader5 : require\n#extension GL_ARB_gpu_shader_fp64 : require\n'+(root/'src/nv2a_gpu_ops.glsl').read_text()+'\n'+(root/'src/nv2a_gpu_clip.glsl').read_text()
feedback_geometry=geometry
geometry=geometry.replace('out vec4 captured[7];','vec4 captured[7];\nout vec4 diffuse,specular;out vec4 texcoord[4];uniform vec2 extent;')
geometry=geometry.replace('gl_Position=vec4(0,0,0,1);EmitVertex();','''float w=captured[0].w;
            gl_Position=vec4((captured[0].xy/extent*2.0-1.0)*w,(captured[0].z/16777215.0*2.0-1.0)*w,w);
            diffuse=captured[1];specular=captured[2];for(int k=0;k<4;k++)texcoord[k]=captured[3+k];
            EmitVertex();''')
if a.two_pass:geometry=feedback_geometry
if a.feedback:
 geometry=geometry.replace('out vec4 diffuse,specular;', 'out vec4 probe_screen;out vec4 diffuse,specular;').replace('float w=captured[0].w;', 'probe_screen=captured[0];float w=captured[0].w;')
(out/'combined.vert').write_text(vertex);(out/'combined.geom').write_text(geometry)
gl=(root/'src/nv2a_gl.c').read_text()
# Remove only the production geometry dispatch from this source-overlay probe;
# the actual integration is tested separately by compare-gpu-geometry.py.
begin=gl.index('    glBindVertexArray(vao);\n    GLuint source=')
end=gl.index('    if(zpass_enabled',begin)
gl=gl[:begin]+'    glBindVertexArray(vao);glBindBuffer(GL_ARRAY_BUFFER,vbo);glBufferData(GL_ARRAY_BUFFER,(size_t)count*sizeof(*v),v,GL_STREAM_DRAW);\n'+gl[end:]
gl=gl.replace('    if(geometry_buffer)glDrawTransformFeedback(GL_TRIANGLES,geometry_feedback);\n    else glDrawArrays(GL_TRIANGLES,0,count);','    glDrawArrays(GL_TRIANGLES,0,count);')

gl=gl.replace('static const char vertex_source[]=','static const char cpu_vertex_source[]=')
if a.feedback:
 gl=gl.replace('out vec4 texcoord[4];void main(){float w=position.w;', 'out vec4 texcoord[4];out vec4 probe_screen;void main(){probe_screen=position;float w=position.w;')
pos=gl.index('static GLuint shader(')
gl=gl[:pos]+('extern int probe_use_gpu;extern float *probe_inputs;extern const float *probe_constants;extern unsigned probe_input_count;extern unsigned *probe_indices;\n'
 + 'static const char *vertex_source=cpu_vertex_source;\n'
 + 'static const char gpu_vertex_source[]='+json.dumps(vertex)+';\n'
 + 'static const char gpu_geometry_source[]='+json.dumps(geometry)+';\n')+gl[pos:]
needle='GLuint vs=shader(GL_VERTEX_SHADER,vertex_source),fs=shader(GL_FRAGMENT_SHADER,ssx_gl_fragment_source);'
assert needle in gl
gl=gl.replace(needle,('' if a.two_pass else 'if(probe_use_gpu)vertex_source=gpu_vertex_source;\n    ')+needle)
for obj in ([] if a.two_pass else ['program','object']):
 needle=f'glAttachShader({obj},fs);glLinkProgram({obj});'
 assert needle in gl
 gl=gl.replace(needle,f'glAttachShader({obj},fs);if(probe_use_gpu){{GLuint gs=shader(GL_GEOMETRY_SHADER,gpu_geometry_source);if(!gs)abort();glAttachShader({obj},gs);glDeleteShader(gs);}}glLinkProgram({obj});')
if a.feedback:
 for obj in ['program','object']:
  gl=gl.replace(f'glLinkProgram({obj});', '{const char *varyings[]={"probe_screen","diffuse","specular","texcoord","gl_Position"};'+f'glTransformFeedbackVaryings({obj},5,varyings,GL_INTERLEAVED_ATTRIBS);'+'}'+f'glLinkProgram({obj});')
 gl=gl.replace('glDrawArrays(GL_TRIANGLES,0,count);', '''GLuint feedback,feedback_query;glGenBuffers(1,&feedback);glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER,feedback);
    glBufferData(GL_TRANSFORM_FEEDBACK_BUFFER,(size_t)count*8*128,NULL,GL_STREAM_READ);glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER,0,feedback);
    glGenQueries(1,&feedback_query);glBeginQuery(GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN,feedback_query);
    glBeginTransformFeedback(GL_TRIANGLES);glDrawArrays(GL_TRIANGLES,0,count);glEndTransformFeedback();glEndQuery(GL_TRANSFORM_FEEDBACK_PRIMITIVES_WRITTEN);
    GLuint primitive_count;glGetQueryObjectuiv(feedback_query,GL_QUERY_RESULT,&primitive_count);size_t size=(size_t)primitive_count*3*128;
    void *data=malloc(size?size:1);if(!data)abort();if(size)glGetBufferSubData(GL_TRANSFORM_FEEDBACK_BUFFER,0,size,data);
    char path[4096];snprintf(path,sizeof path,"%s.feedback.bin",getenv("SSX_DRAW_OUTPUT"));FILE *f=fopen(path,"wb");
    if(!f || fwrite(data,1,size,f)!=size || fclose(f))abort();free(data);
    glDeleteQueries(1,&feedback_query);glDeleteBuffers(1,&feedback);''')
old='for(int i=0;i<7;i++){glEnableVertexAttribArray(i);glVertexAttribPointer(i,4,GL_FLOAT,GL_FALSE,sizeof(SsxGlVertex),(void*)(uintptr_t)(i*16));}'
assert old in gl
if not a.two_pass:gl=gl.replace(old,'for(int i=0;i<(probe_use_gpu?16:7);i++){glEnableVertexAttribArray(i);glVertexAttribPointer(i,4,GL_FLOAT,GL_FALSE,probe_use_gpu?256:sizeof(SsxGlVertex),(void*)(uintptr_t)(i*16));}')
old='glBufferData(GL_ARRAY_BUFFER,(size_t)count*sizeof(*v),v,GL_STREAM_DRAW);'
assert old in gl
gl=gl.replace(old,'''glBufferData(GL_ARRAY_BUFFER,(size_t)count*(probe_use_gpu?256:sizeof(*v)),probe_use_gpu?(const void*)probe_inputs:(const void*)v,GL_STREAM_DRAW);
    if(probe_use_gpu){
        glUniform4fv(glGetUniformLocation(program,"context_bank"),192,probe_constants);
        glUniform4f(glGetUniformLocation(program,"clip_bounds"),d->clip_x,d->clip_y,d->clip_w,d->clip_h);
        float far;memcpy(&far,&r[0x398/4],4);if(!isfinite(far)||far<=0)far=16777215.f;
        glUniform1f(glGetUniformLocation(program,"clip_far"),far);
        glUniform1i(glGetUniformLocation(program,"cull_enable"),r[0x308/4]!=0);
        glUniform1i(glGetUniformLocation(program,"front_clockwise"),r[0x3a0/4]==0x900);
        glUniform1i(glGetUniformLocation(program,"cull_face"),r[0x39c/4]);
    }''')
if a.two_pass:
 # Undo the single-pass upload replacement; geometry uses separate GPU buffers.
 begin=gl.index('glBufferData(GL_ARRAY_BUFFER,(size_t)count*(probe_use_gpu?256:sizeof(*v))')
 end=gl.index('    if(zpass_enabled',begin)
 gl=gl[:begin]+'''if(probe_use_gpu){
        glUseProgram(probe_geometry_program);glBindVertexArray(probe_geometry_vao);
        glBindBuffer(GL_ARRAY_BUFFER,probe_geometry_input);glBufferData(GL_ARRAY_BUFFER,(size_t)count*256,probe_inputs,GL_STREAM_DRAW);
        glUniform4fv(glGetUniformLocation(probe_geometry_program,"context_bank"),192,probe_constants);
        glUniform4f(glGetUniformLocation(probe_geometry_program,"clip_bounds"),d->clip_x,d->clip_y,d->clip_w,d->clip_h);
        float far;memcpy(&far,&r[0x398/4],4);if(!isfinite(far)||far<=0)far=16777215.f;
        glUniform1f(glGetUniformLocation(probe_geometry_program,"clip_far"),far);
        glUniform1i(glGetUniformLocation(probe_geometry_program,"cull_enable"),r[0x308/4]!=0);
        glUniform1i(glGetUniformLocation(probe_geometry_program,"front_clockwise"),r[0x3a0/4]==0x900);
        glUniform1i(glGetUniformLocation(probe_geometry_program,"cull_face"),r[0x39c/4]);
        glBindTransformFeedback(GL_TRANSFORM_FEEDBACK,probe_feedback_object);
        glBindBuffer(GL_TRANSFORM_FEEDBACK_BUFFER,probe_geometry_output);
        glBufferData(GL_TRANSFORM_FEEDBACK_BUFFER,(size_t)count*8*112,NULL,GL_STREAM_DRAW);
        glBindBufferBase(GL_TRANSFORM_FEEDBACK_BUFFER,0,probe_geometry_output);
        glEnable(GL_RASTERIZER_DISCARD);glBeginTransformFeedback(GL_TRIANGLES);
        glDrawArrays(GL_TRIANGLES,0,count);glEndTransformFeedback();glDisable(GL_RASTERIZER_DISCARD);
        glUseProgram(program);glBindVertexArray(vao);glBindBuffer(GL_ARRAY_BUFFER,probe_geometry_output);
        for(int i=0;i<7;i++)glVertexAttribPointer(i,4,GL_FLOAT,GL_FALSE,112,(void*)(uintptr_t)(i*16));
    }else glBufferData(GL_ARRAY_BUFFER,(size_t)count*sizeof(*v),v,GL_STREAM_DRAW);
'''+gl[end:]
 # Replace only the final raster draw; retain the prepass draw above.
 at=gl.index('    if(zpass_enabled')
 gl=gl[:at]+gl[at:].replace('glDrawArrays(GL_TRIANGLES,0,count);', 'if(probe_use_gpu)glDrawTransformFeedback(GL_TRIANGLES,probe_feedback_object);else glDrawArrays(GL_TRIANGLES,0,count);',1)
 gl=gl.replace('static GLuint program,vao,vbo,fbo,samplers[4];',
  'static GLuint program,vao,vbo,fbo,samplers[4];\nstatic GLuint probe_geometry_program,probe_geometry_vao,probe_geometry_input,probe_geometry_output,probe_feedback_object;')
 needle='    GLuint vs=shader(GL_VERTEX_SHADER,vertex_source),fs=shader(GL_FRAGMENT_SHADER,ssx_gl_fragment_source);'
 assert needle in gl
 init='''    if(probe_use_gpu){
        if(!epoxy_has_gl_extension("GL_ARB_transform_feedback2")){fprintf(stderr,"Missing transform feedback2\\n");exit(77);}
        GLuint pvs=shader(GL_VERTEX_SHADER,gpu_vertex_source),pgs=shader(GL_GEOMETRY_SHADER,gpu_geometry_source);
        if(!pvs||!pgs)abort();probe_geometry_program=glCreateProgram();glAttachShader(probe_geometry_program,pvs);glAttachShader(probe_geometry_program,pgs);
        const char *varying="captured";glTransformFeedbackVaryings(probe_geometry_program,1,&varying,GL_INTERLEAVED_ATTRIBS);
        glLinkProgram(probe_geometry_program);glDeleteShader(pvs);glDeleteShader(pgs);GLint linked;glGetProgramiv(probe_geometry_program,GL_LINK_STATUS,&linked);
        if(!linked){char log[4096];glGetProgramInfoLog(probe_geometry_program,sizeof log,NULL,log);fprintf(stderr,"%s\\n",log);abort();}
        glGenVertexArrays(1,&probe_geometry_vao);glBindVertexArray(probe_geometry_vao);
        glGenBuffers(1,&probe_geometry_input);glBindBuffer(GL_ARRAY_BUFFER,probe_geometry_input);
        for(int i=0;i<16;i++){glEnableVertexAttribArray(i);glVertexAttribPointer(i,4,GL_FLOAT,GL_FALSE,256,(void*)(uintptr_t)(i*16));}
        glGenBuffers(1,&probe_geometry_output);glGenTransformFeedbacks(1,&probe_feedback_object);
    }
'''
 gl=gl.replace(needle,init+needle)
if a.indexed:
 gl=gl.replace('probe_geometry_output,probe_feedback_object;', 'probe_geometry_output,probe_feedback_object,probe_geometry_indices;')
 gl=gl.replace('glGenBuffers(1,&probe_geometry_output);glGenTransformFeedbacks', 'glGenBuffers(1,&probe_geometry_indices);glGenBuffers(1,&probe_geometry_output);glGenTransformFeedbacks')
 gl=gl.replace('glBufferData(GL_ARRAY_BUFFER,(size_t)count*256,probe_inputs,GL_STREAM_DRAW);',
  'glBufferData(GL_ARRAY_BUFFER,(size_t)probe_input_count*256,probe_inputs,GL_STREAM_DRAW);glBindBuffer(GL_ELEMENT_ARRAY_BUFFER,probe_geometry_indices);glBufferData(GL_ELEMENT_ARRAY_BUFFER,(size_t)count*4,probe_indices,GL_STREAM_DRAW);')
 gl=gl.replace('glDrawArrays(GL_TRIANGLES,0,count);glEndTransformFeedback();glDisable(GL_RASTERIZER_DISCARD);',
  'glDrawElements(GL_TRIANGLES,count,GL_UNSIGNED_INT,0);glEndTransformFeedback();glDisable(GL_RASTERIZER_DISCARD);')
gl='#include <math.h>\n'+gl
(out/'gl.c').write_text(gl)
pb=(root/'third_party/xboxrecomp/src/kernel/nv2a_pb_exec.c').read_text()
needle='static void raster_indexed(uint32_t i0, uint32_t i1, uint32_t i2, uint32_t argb)\n{'
assert needle in pb
pb=pb.replace(needle,needle+'''
    probe_triangles++;
    if(probe_use_gpu){
        if(!ssx_vertex_active() || !s_gl_collect)abort();
        if(surface_bpp()!=4 || !surface_begin_batch((const uint8_t*)xbox_GetMemoryOffset()))return;
        if(s_gl_count+3>probe_capacity){probe_capacity=probe_capacity?probe_capacity*2:3072;
            probe_inputs=realloc(probe_inputs,(size_t)probe_capacity*256);if(!probe_inputs)abort();}
        uint32_t indices[3]={i0,i1,i2};
        for(unsigned k=0;k<3;k++)memcpy(probe_inputs+(s_gl_count+k)*64,fetch_shader_vertex_inputs(indices[k]),256);
        s_gl_count+=3;s_gl_shader=1;s_gl_textured=1;return;
    }
''')
pb=pb.replace('static void raster_batch(void)\n{', 'static void raster_batch(void)\n{\n    probe_input_count=0;probe_cpu_vertices=0;')
pb=pb.replace('if (!ssx_vertex_execute(in, out)) return;', 'probe_cpu_vertices++;if (!ssx_vertex_execute(in, out)) return;')
if a.indexed:
 old='for(unsigned k=0;k<3;k++)memcpy(probe_inputs+(s_gl_count+k)*64,fetch_shader_vertex_inputs(indices[k]),256);'
 assert old in pb
 pb=pb.replace('probe_inputs=realloc(probe_inputs,(size_t)probe_capacity*256);if(!probe_inputs)abort();',
  'probe_inputs=realloc(probe_inputs,(size_t)probe_capacity*256);probe_indices=realloc(probe_indices,(size_t)probe_capacity*4);if(!probe_inputs||!probe_indices)abort();')
 pb=pb.replace(old,'''for(unsigned k=0;k<3;k++){
            unsigned slot=indices[k]&255;
            if(probe_cache[slot].generation!=s_vertex_generation || probe_cache[slot].index!=indices[k]){
                probe_cache[slot].index=indices[k];probe_cache[slot].generation=s_vertex_generation;probe_cache[slot].dense=probe_input_count++;
                memcpy(probe_inputs+probe_cache[slot].dense*64,fetch_shader_vertex_inputs(indices[k]),256);
            }
            probe_indices[s_gl_count+k]=probe_cache[slot].dense;
        }''')
else:pb=pb.replace('s_gl_count+=3;s_gl_shader=1;', 's_gl_count+=3;probe_input_count=s_gl_count;s_gl_shader=1;')
(out/'pb.inc').write_text(pb)
source=(root/'tools/draw_replay.c').read_text()
# The production replay now has its own metrics; this older diagnostic keeps
# its separate readback/vertex instrumentation for arithmetic investigations.
start=source.index('    ssx_gl_sync();\n    unsigned height=')
end=source.index('    s_gpu.drawn_offset=s_gpu.color_offset;',start)
source=source[:start]+'    raster_batch();\n'+source[end:]
source=source.replace('#include "../third_party/xboxrecomp/src/kernel/nv2a_pb_exec.c"','#include "pb.inc"')
source=source.replace('#include "../src/nv2a_vertex.c"','#include '+json.dumps(str(root/'src/nv2a_vertex.c')))
source='''#include <time.h>
#include <assert.h>
#include <stdlib.h>
int probe_use_gpu;float *probe_inputs;const float *probe_constants;
unsigned probe_input_count,*probe_indices;static unsigned probe_cpu_vertices;
static struct {unsigned index,generation,dense;} probe_cache[256];
static unsigned probe_capacity,probe_triangles;
static double probe_seconds(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec+t.tv_nsec*1e-9;}
'''+source
source=source.replace('    s_render_every=1;s_skip_frame=0;','    probe_use_gpu=getenv("SSX_DRAW_GPU")!=NULL;probe_constants=constants;\n    s_render_every=1;s_skip_frame=0;')
# Production-equivalent native dispatch is enabled in this translation unit.
needle='''    raster_batch();
    s_gpu.drawn_offset=s_gpu.color_offset;
    dump_surface_bmp();'''
assert needle in source
source=source.replace(needle,'''    unsigned height=s_gpu.clip_y+s_gpu.clip_h;size_t color_bytes=(size_t)s_gpu.pitch*height;
    size_t depth_bytes=s_gpu.depth_offset?(size_t)s_gpu.depth_pitch*height:0;
    void *color=(char*)ram+dma_resolve(s_gpu.color_offset),*depth=(char*)ram+dma_resolve(s_gpu.depth_offset);
    unsigned char *initial=malloc(color_bytes),*initial_depth=malloc(depth_bytes?depth_bytes:1);assert(initial&&initial_depth);
    ssx_gl_sync();memcpy(initial,color,color_bytes);if(depth_bytes)memcpy(initial_depth,depth,depth_bytes);
    unsigned repeats=strtoul(getenv("SSX_DRAW_RUNS"),NULL,10),warmup=strtoul(getenv("SSX_DRAW_WARMUP"),NULL,10);
    double *submission=calloc(repeats,sizeof(double));assert(submission);
    printf("{\\"gpu\\":%d,\\"indices\\":%u,\\"width\\":%u,\\"height\\":%u,\\"seconds\\":[",probe_use_gpu,s_gpu.idx_count,s_gpu.pitch/4,height);
    for(unsigned run=0;run<repeats+warmup;run++){
        memcpy(color,initial,color_bytes);if(depth_bytes)memcpy(depth,initial_depth,depth_bytes);ssx_gl_invalidate();probe_triangles=0;
        double before=probe_seconds();raster_batch();double submitted=probe_seconds()-before;ssx_gl_sync();double elapsed=probe_seconds()-before;
        if(run>=warmup)submission[run-warmup]=submitted;
        if(run>=warmup)printf("%s%.9g",run==warmup?"":",",elapsed);
    }
    unsigned changed=0;for(size_t i=0;i<color_bytes;i+=4)changed+=memcmp((char*)color+i,initial+i,4)!=0;
    printf("],\\"submission_seconds\\":[");for(unsigned run=0;run<repeats;run++)printf("%s%.9g",run?",":"",submission[run]);free(submission);
    printf("],\\"input_triangles\\":%u,\\"changed_color_pixels\\":%u,\\"native_program_matched\\":%d,\\"native_constants_finite\\":%d,\\"input_vertex_uploads\\":%u,\\"cpu_vertex_calls\\":%u}\\n",probe_triangles,changed,native_program!=NULL,native_context_finite,probe_input_count,probe_cpu_vertices);
    const char *prefix=getenv("SSX_DRAW_OUTPUT");char path[4096];
    snprintf(path,sizeof path,"%s.color.bin",prefix);FILE *f=fopen(path,"wb");assert(f&&fwrite(color,1,color_bytes,f)==color_bytes);assert(!fclose(f));
    snprintf(path,sizeof path,"%s.depth.bin",prefix);f=fopen(path,"wb");assert(f&&fwrite(depth,1,depth_bytes,f)==depth_bytes);assert(!fclose(f));
    s_gpu.drawn_offset=s_gpu.color_offset;dump_surface_bmp();
    free(initial);free(initial_depth);free(probe_inputs);free(probe_indices);free(s_gl_vertices);munmap(ram,UINT64_C(0x100000000));''')
(out/'replay.c').write_text(source)
commands=subprocess.check_output(['ninja','-C',str(build),'-t','commands','draw_replay'],text=True).splitlines()
objects=[]
for filename,match in [('replay','/tools/draw_replay.c'),('gl','/src/nv2a_gl.c')]:
 command=next(shlex.split(x) for x in commands if ' -c ' in x and match in x)
 obj=out/(filename+'.o');objects.append(obj)
 for flag,value in [('-c',out/(filename+'.c')),('-o',obj),('-MF',out/(filename+'.d')),('-MT',obj)]:
  if flag in command:command[command.index(flag)+1]=str(value)
 if filename=='replay':command+=['-DSSX_NATIVE_VERTEX']
 subprocess.run(command,cwd=build,check=True)
link=[x for x in shlex.split(commands[-1])[2:-2] if not x.startswith('-Wl,--dependency-file=')]
index=link.index('CMakeFiles/draw_replay.dir/tools/draw_replay.c.o');link[index:index+1]=list(map(str,objects))
exe=out/'replay';link[link.index('-o')+1]=str(exe);subprocess.run(link,cwd=build,check=True)
spec=importlib.util.spec_from_file_location('launcher',root/'scripts/play-linux.py');launcher=importlib.util.module_from_spec(spec);spec.loader.exec_module(launcher)
env=dict(os.environ,SSX_GL_GPU_GEOMETRY='0',SSX_GL='1',SSX_VSH_NATIVE='1',SSX_GL_SPECIALIZE='1',SSX_DRAW_RUNS=str(a.runs),SSX_DRAW_WARMUP=str(a.warmup));env.pop('SSX_DRAW_GPU',None);env.pop('SSX_VSH_TRACE',None)
env['SSX_REPLAY_OPTIONS']='clear' if a.clear else ''
launcher.matching_nvidia_libraries(env,out/'env')
report={'capture':str(capture),'shader':number,'cleared':a.clear,'runs':a.runs,'diagnostic_feedback':a.feedback,'two_pass':a.two_pass,'gpu_first':a.gpu_first,'warmup':a.warmup,'indexed':a.indexed}
for name in (['gpu','cpu'] if a.gpu_first else ['cpu','gpu']):
 prefix=str(a.report_prefix.resolve())+'-'+name;env['SSX_DRAW_OUTPUT']=prefix;env['RECOMP_FB_DUMP']=prefix
 env.pop('SSX_DRAW_GPU',None)
 if name=='gpu':env['SSX_DRAW_GPU']='1'
 with open(prefix+'.json','w') as stdout,open(prefix+'.log','w') as stderr:
  result=subprocess.run([str(exe),str(capture)],env=env,stdout=stdout,stderr=stderr)
 if result.returncode:raise SystemExit(f'{name} replay failed {result.returncode}; see {prefix}.log')
 report[name]=json.loads(Path(prefix+'.json').read_text())
for kind in ['color','depth']:
 blobs=[Path(str(a.report_prefix.resolve())+'-'+name+'.'+kind+'.bin').read_bytes() for name in ['cpu','gpu']]
 assert len(blobs[0])==len(blobs[1])
 report[kind]={'bytes':len(blobs[0]),'different_words':sum(blobs[0][i:i+4]!=blobs[1][i:i+4] for i in range(0,len(blobs[0]),4)),'sha256':[hashlib.sha256(b).hexdigest() for b in blobs]}
Path(str(a.report_prefix)+'.json').write_text(json.dumps(report,indent=2)+'\n');print(json.dumps(report,indent=2))
raise SystemExit(int(any(report[k]['different_words'] for k in ['color','depth'])))
