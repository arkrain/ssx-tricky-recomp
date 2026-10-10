#!/usr/bin/env python3
"""Embed the checked experimental GPU geometry programs for an opt-in renderer."""
import argparse,json,struct,subprocess
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--native-inc',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args();root=Path(__file__).resolve().parents[1];work=a.output.parent/'gpu_geometry'
subprocess.run(['python3',str(root/'scripts/generate-vertex-glsl-probe.py'),'--native-inc',str(a.native_inc),'--output-dir',str(work)],check=True)
manifest=json.loads((work/'manifest.json').read_text());sections=[];entries=[]
# Initial gameplay experiment: visible, exact color/depth replay coverage in160.
eligible={1,2,6,10,16}
for item in manifest:
 n=item['program'];v=(work/f'program{n}.vert').read_text()
 old='precise out vec4 captured[13];\nout float valid_result;';assert old in v
 v=v.replace(old,'out VertexData { vec4 values[7]; } vertex;')
 begin=v.index('    valid_result=');end=v.index('    gl_Position=',begin)
 v=v[:begin]+'''    for(int k=0;k<4;k++)vertex.values[0][k]=valid?o[k]:uintBitsToFloat(0x7fc00000u);
    for(int k=0;k<8;k++){float x=o[12+k];vertex.values[1+k/4][k%4]=x<0.0?0.0:x<1.0?x:1.0;}
    for(int k=0;k<16;k++)vertex.values[3+k/4][k%4]=o[36+k];
'''+v[end:]
 b=(work/f'program{n}-code.bin').read_bytes();words=struct.unpack('<'+str(len(b)//4)+'I',b)
 sections.append(f'static const uint32_t gpu_code_{n}[]={{'+','.join(hex(x) for x in words)+'};')
 sections.append(f'static const char gpu_source_{n}[]='+json.dumps(v)+';')
 entries.append(f'{{gpu_code_{n},{len(b)//16},gpu_source_{n},{int(n in eligible)}}}')
g='#version 330 core\n#extension GL_ARB_gpu_shader5 : require\n#extension GL_ARB_gpu_shader_fp64 : require\n'+(root/'src/nv2a_gpu_ops.glsl').read_text()+'\n'+(root/'src/nv2a_gpu_clip.glsl').read_text()
sections.append('static const char gpu_clip_source[]='+json.dumps(g)+';')
sections.append('static const struct {const uint32_t *words;unsigned count;const char *source;int eligible;} gpu_catalog[]={'+','.join(entries)+'};')
a.output.write_text('\n'.join(sections)+'\n')
