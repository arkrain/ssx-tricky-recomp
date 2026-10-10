#!/usr/bin/env python3
"""Differentially check complete clipped vertex streams against a snapshot.

Run with the game closed. This is a deterministic correctness stress probe,
not a frame-rate benchmark. Both versions also execute the raster assertions.
"""
import argparse
import json
import os
import shlex
import struct
import subprocess
import tempfile
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--baseline', type=Path, required=True)
p.add_argument('--harness', type=Path, required=True)
p.add_argument('--report', type=Path, required=True)
p.add_argument('--build-dir', type=Path, help='Configured Ninja build (default: build/linux-release)')
a = p.parse_args()
root = Path(__file__).resolve().parents[1]
build = a.build_dir.resolve() if a.build_dir else root / 'build/linux-release'
commands = subprocess.check_output(
    ['ninja', '-C', str(build), '-t', 'commands', 'raster_test'], text=True).splitlines()
compile = next(shlex.split(x) for x in commands if ' -c ' in x and '/tests/raster.c' in x)
link = [x for x in shlex.split(commands[-1])[2:-2]
        if not x.startswith('-Wl,--dependency-file=')]
helpers = r'''
static uint32_t probe_random_state=0x1505a17u;
static uint32_t probe_random(void) {
    uint32_t x=probe_random_state;
    x^=x<<13;x^=x>>17;x^=x<<5;
    return probe_random_state=x;
}
static float probe_value(void) {
    return ((int)(probe_random()%8193)-4096)/32.0f;
}
'''
probe = r'''
    memset(&s_gpu,0,sizeof s_gpu);memset(s_render_reg,0,sizeof s_render_reg);
    s_gpu.pitch=64;s_gpu.color_offset=0x300000;s_triangle_colors=1;
    FILE *dump=fopen(getenv("SSX_CLIP_PROBE_DUMP"),"wb");assert(dump);
    uint64_t hash=1469598103934665603ULL,submitted=0,visible=0;
    for(unsigned trial=0;trial<150000;trial++) {
        s_gpu.clip_x=trial%4;s_gpu.clip_y=trial%7;
        s_gpu.clip_w=16+trial%23;s_gpu.clip_h=16+trial%31;
        s_gpu.tex.color=trial%2?6:0;s_gpu.tex.width=16;s_gpu.tex.height=32;
        float far=trial%37==0?NAN:trial%41==0?-1:trial%43==0?INFINITY:16;
        memcpy(&s_render_reg[0x398/4],&far,4);
        float position[3][4];
        for(unsigned v=0;v<3;v++) {
            for(unsigned k=0;k<3;k++)position[v][k]=probe_value();
            position[v][3]=.25f+(probe_random()%16)*.25f;
            if(trial%8==1)position[v][3]=probe_value();
            if(trial%8==2) {
                position[v][0]=s_gpu.clip_x+(v==0?-1e-6f:v==1?0:1e-6f);
                position[v][1]=4+v*3;position[v][2]=2;
            }
            if(trial%8==3) {
                position[v][0]=v==0?3e38f:v==1?-3e38f:8;
                position[v][1]=v==0?-3e38f:v==1?3e38f:8;
                position[v][2]=2;position[v][3]=v==2?1:3e38f;
            }
            if(trial%8==4)position[v][3]=v==0?-1e-5f:v==1?1e-5f:1;
            if(trial%8==5) {
                position[v][0]=v==0?-4:v==1?12:4;
                position[v][1]=v==2?12:4;position[v][2]=2;
                position[v][3]=1;
            }
            if(trial%8==6)position[v][2]=v==0?-1e-6f:v==1?16.000002f:8;
            if(trial%8==7 && v==0)position[v][trial%4]=trial%2?NAN:INFINITY;
            float varying[24];
            for(unsigned k=0;k<24;k++)varying[k]=probe_value();
            if(trial%11==0)varying[(trial+v)%24]=NAN;
            if(trial%13==0)varying[(trial+v*2)%24]=INFINITY;
            if(trial%17==0)varying[(trial+v*3)%24]=-INFINITY;
            if(trial%19==0)varying[(trial+v*5)%24]=-0.0f;
            memcpy(s_triangle_color[v],varying,32);
            memcpy(s_triangle_tex[v],varying+8,64);
        }
        s_gl_collect=1;s_gl_count=0;
        raster_shader_triangle(position,0xff123456);
        uint32_t vertex_count=s_gl_count;
        assert(fwrite(&vertex_count,4,1,dump)==1);
        assert(fwrite(s_gl_vertices,sizeof(*s_gl_vertices),s_gl_count,dump)==s_gl_count);
        submitted+=s_gl_count;visible+=s_gl_count!=0;
        hash=(hash^s_gl_count)*1099511628211ULL;
        for(size_t k=0;k<s_gl_count*sizeof(*s_gl_vertices);k++)
            hash=(hash^((unsigned char*)s_gl_vertices)[k])*1099511628211ULL;
    }
    printf("%016llx %llu %llu\n",(unsigned long long)hash,
           (unsigned long long)submitted,(unsigned long long)visible);
    assert(!fclose(dump));
'''
report = {'cases': 150000}
with tempfile.TemporaryDirectory(prefix='ssx-polygon-clip-') as work:
    for name, source in [('old', a.baseline.resolve()),
                         ('new', root / 'third_party/xboxrecomp/src/kernel/nv2a_pb_exec.c')]:
        c = Path(work) / (name + '.c')
        obj, exe = c.with_suffix('.o'), c.with_suffix('')
        body = a.harness.read_text().replace(
            '#include "../third_party/xboxrecomp/src/kernel/nv2a_pb_exec.c"',
            '#include "' + str(source) + '"').replace(
            'int main(void)', helpers + '\nint main(void)').replace(
            '    free(ram); return 0;', probe + '\n    free(ram); return 0;')
        c.write_text(body)
        cmd = compile.copy()
        cmd[cmd.index('-o') + 1] = str(obj)
        cmd[cmd.index('-c') + 1] = str(c)
        if '-MF' in cmd:
            cmd[cmd.index('-MF') + 1] = str(c.with_suffix('.d'))
        if '-MT' in cmd:
            cmd[cmd.index('-MT') + 1] = str(obj)
        subprocess.run(cmd, cwd=build, check=True, stdout=subprocess.DEVNULL)
        cmd = link.copy()
        cmd[cmd.index('CMakeFiles/raster_test.dir/tests/raster.c.o')] = str(obj)
        cmd[cmd.index('-o') + 1] = str(exe)
        subprocess.run(cmd, cwd=build, check=True)
        env = dict(os.environ, SSX_CLIP_PROBE_DUMP=str(Path(work) / (name + '.bin')))
        result = subprocess.run([str(exe)], env=env, capture_output=True, text=True, check=True)
        digest, vertices, visible = result.stdout.split()
        report[name] = {'hash': digest, 'submitted_vertices': int(vertices),
                        'visible_cases': int(visible)}
    if report['old'] != report['new']:
        streams = [(Path(work) / (name + '.bin')).open('rb') for name in ('old', 'new')]
        differences = []
        for trial in range(report['cases']):
            counts = [struct.unpack('<I', f.read(4))[0] for f in streams]
            data = [f.read(n * 112) for f, n in zip(streams, counts)]
            if data[0] != data[1] or counts[0] != counts[1]:
                lanes = []
                for offset in range(0, min(map(len, data)), 4):
                    values = [v[offset:offset + 4] for v in data]
                    if values[0] != values[1]:
                        lanes.append({'vertex': offset // 112, 'lane': offset % 112 // 4,
                                      'bits': [hex(struct.unpack('<I', v)[0]) for v in values]})
                differences.append({'trial': trial, 'counts': counts, 'lanes': lanes[:10]})
                if len(differences) == 10:
                    break
        for f in streams:
            f.close()
        report['first_differences'] = differences
        a.report.write_text(json.dumps(report, indent=2) + '\n')
        raise SystemExit('Clipped vertex streams differ: ' + json.dumps(report))
a.report.write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
