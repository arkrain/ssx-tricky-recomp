#!/usr/bin/env python3
"""Translate the bounded generated native programs for an offline GLSL probe.

This uses the current generator's explicitly checked output format, shared
with the opt-in GPU geometry experiment; it is not a general C translator. Generated
programs/code are original game data and must remain outside portable bundles.
"""
import argparse
import hashlib
import json
import re
import struct
from pathlib import Path

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--native-inc', type=Path, required=True)
p.add_argument('--output-dir', type=Path, required=True)
p.add_argument('--trace-step', type=int)
a = p.parse_args()
root = Path(__file__).resolve().parents[1]
ops = (root / 'src/nv2a_gpu_ops.glsl').read_text()
source = a.native_inc.read_text()
programs = re.findall(r'static const uint32_t native_code_(\d+)\[(\d+)\]\[4\]=\{(.*?)\};\n'
                      r'static int native_program_\1\(const float \*v,const float \*c,float \*o\)\{(.*?)return 1;}',
                      source, re.S)
assert programs and len(programs) == source.count('static int native_program_')
a.output_dir.mkdir(parents=True, exist_ok=True)
manifest = []
trace_functions = []
for number, length, code, body in programs:
    words = [int(x, 16) for x in re.findall(r'0x[0-9a-f]+', code)]
    assert len(words) == int(length)*4
    for op in set(re.findall(r'ssx_native_op_\w+', body)):
        assert 'void ' + op + '(' in ops, 'Unimplemented probe opcode: ' + op
    operation_names = re.findall(r'ssx_native_op_(\w+)\(result,[mu]\);', body)
    if a.trace_step is not None:
        call_index = [0]
        def instrument(match):
            index = call_index[0]
            call_index[0] += 1
            if index != a.trace_step:
                return match[0]
            bank = match[1]
            return (match[0]+'\nfor(int k=0;k<52;k++)o[k]=0;'
                    +f'for(int k=0;k<12;k++)o[k]={bank}[k];'
                    +'for(int k=0;k<4;k++)o[k+12]=result[k];return 1;')
        traced = re.sub(r'ssx_native_op_\w+\(result,([mu])\);', instrument, body)
        trace_functions.append(f'static int trace_{number}(const float *v,const float *c,float *o){{'
                               +traced+'return 1;}')
        body = traced
    initial = 'float r[48]={0},a=0,result[4];'
    assert body.startswith('\n'+initial)
    body = body.replace(initial, 'precise float r[48];for(int k=0;k<48;k++)r[k]=0.0;'
                        'precise float a=0.0;precise float result[4];', 1)
    body = re.sub(r'float ([mu])\[12\]=\{0\};',
                  r'precise float \1[12];for(int k=0;k<12;k++)\1[k]=0.0;', body)
    body = body.replace('(int)a', 'int(a)').replace('isfinite(', 'finite_value(')
    body = body.replace('return 0;', 'return false;').replace('return 1;', 'return true;')
    body = re.sub(r'c\[([^\]]+)\]', r'read_constant(\1)', body)
    body = re.sub(r'(?<=\d)f\b', '', body)
    body = re.sub(r'(-?)1\.0\*(v\[\d+\]|r\[\d+\]|read_constant\([^;]+?\))',
                  lambda m: 'signed_input('+m[2]+','+('true' if m[1] else 'false')+')', body)
    glsl = '''#version 330 core
#extension GL_ARB_gpu_shader5 : require
#extension GL_ARB_gpu_shader_fp64 : require
layout(location=0) in vec4 attributes[16];
uniform vec4 context_bank[192];
precise out vec4 captured[13];
out float valid_result;
float read_constant(int index){return context_bank[index/4][index%4];}
''' + ops + '\nbool execute(float v[64],inout float o[52]){' + body + '''return true;}
void main(){
    float v[64];precise float o[52];
    for(int k=0;k<64;k++)v[k]=attributes[k/4][k%4];
    for(int k=0;k<52;k++)o[k]=k%4==3?1.0:0.0;
    bool valid=execute(v,o);
    valid_result=valid && finite_value(o[0]) && finite_value(o[1])?1.0:0.0;
    for(int k=0;k<52;k++)captured[k/4][k%4]=o[k];
    gl_Position=vec4(0.0,0.0,0.0,1.0);
}
'''
    (a.output_dir / f'program{number}.vert').write_text(glsl)
    binary = struct.pack('<'+'I'*len(words), *words)
    (a.output_dir / f'program{number}-code.bin').write_bytes(binary)
    manifest.append({'program': int(number), 'instructions': int(length),
                     'code_sha256': hashlib.sha256(binary).hexdigest(), 'operations': operation_names})
(a.output_dir / 'manifest.json').write_text(json.dumps(manifest, indent=2)+'\n')
print(f'Generated {len(manifest)} experimental vertex shaders in {a.output_dir}')

if a.trace_step is not None:
    native = '#include "nv2a_native_ops.inc"\n' + '\n'.join(trace_functions)
    native += '\nint probe_trace(unsigned n,const float *v,const float *c,float *o){switch(n){'
    native += ''.join(f'case {m["program"]}:return trace_{m["program"]}(v,c,o);' for m in manifest)
    native += 'default:return 0;}}\n'
    (a.output_dir/'trace_native.c').write_text(native)
