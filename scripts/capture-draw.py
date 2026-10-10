#!/usr/bin/env python3
"""Write a GDB script to capture an indexed 3D batch for draw_replay.

Usage: python3 scripts/capture-draw.py BINARY DIRECTORY > /tmp/capture.gdb
       gdb -batch BINARY -p PID -x /tmp/capture.gdb
The snapshot contains original game assets and is only for local diagnostics.
"""
import argparse
import pathlib
import subprocess
import sys
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('binary',type=pathlib.Path)
parser.add_argument('directory',type=pathlib.Path)
parser.add_argument('--components',type=int,default=3,choices=range(1,5))
parser.add_argument('--stride',type=int)
parser.add_argument('--primitive',type=int)
parser.add_argument('--second-components',type=int,choices=range(0,5))
parser.add_argument('--at-gl',action='store_true',help='Capture at host draw submission instead of every indexed triangle; also dump transformed vertices')
parser.add_argument('--vertex-mode',choices=('any','fixed','programmable'),default='any')
parser.add_argument('--min-indices',type=int,default=3)
parser.add_argument('--blend',action='store_true',help='Require blending enabled')
parser.add_argument('--blend-src',type=lambda value:int(value,0))
parser.add_argument('--blend-dst',type=lambda value:int(value,0))
args=parser.parse_args()
if not 3<=args.min_indices<=4096:parser.error('--min-indices must be between 3 and 4096')
binary,directory=args.binary,args.directory
directory = directory.resolve()
directory.mkdir(parents=True, exist_ok=True)
names = {'s_gpu','s_render_reg','s_tex_reg','s_tex_set','s_color_write_mask',
         'words','constants','start','mode'}
symbols={}
for line in subprocess.check_output(['nm','-S',str(binary)],text=True).splitlines():
    fields=line.split()
    if len(fields)==4 and fields[3] in names:
        symbols[fields[3]]=int(fields[1],16)
if set(symbols)!=names: raise SystemExit('missing capture symbols')
if symbols['s_gpu']!=25352:raise SystemExit('GPU state layout changed; update capture offsets before use')
print('set pagination off\nset confirm off\nset breakpoint pending on\nset debuginfod enabled off')
print('handle SIGUSR1 nostop noprint pass\nhandle SIGUSR2 nostop noprint pass')
conditions=[f'((unsigned int*)&s_gpu)[2] == {args.components}']
for index,value in [(3,args.stride),(77,args.primitive),(6,args.second_components),(74,args.blend_src),(75,args.blend_dst)]:
    if value is not None:conditions.append(f'((unsigned int*)&s_gpu)[{index}] == {value}')
conditions.append(f'((unsigned int*)&s_gpu)[2126] >= {args.min_indices}')
if args.blend:conditions.append('((unsigned int*)&s_gpu)[73] != 0')
if args.vertex_mode!='any':conditions.append('((*(unsigned int*)&mode)&3) '+('==' if args.vertex_mode=='programmable' else '!=')+' 2')
print('set $ssx_capture_hit=0')
print('tbreak '+('ssx_gl_draw' if args.at_gl else 'raster_indexed')+' if '+' && '.join(conditions))
print('commands\nsilent\nset $ssx_capture_hit=1\nend')
print('continue')
print('if $ssx_capture_hit == 0\nprintf "Capture interrupted before a matching draw\\n"\ndetach\nquit\nend')
print('set $ssx_capture_mem=(long)g_xbox_mem_offset')
print(f'dump binary memory {directory}/physical.bin $ssx_capture_mem+0x80000000 $ssx_capture_mem+0x84000000')
for name,size in symbols.items():
    print(f'dump binary memory {directory}/{name}.bin (char*)&{name} (char*)&{name}+{size}')
if args.at_gl:
    print('set $ssx_capture_vertices=*(char**)&s_gl_vertices')
    print('set $ssx_capture_count=*(unsigned int*)&s_gl_count')
    print(f'dump binary memory {directory}/gl_vertices.bin $ssx_capture_vertices $ssx_capture_vertices+$ssx_capture_count*112')
    print('printf "Captured %u transformed vertices\\n",$ssx_capture_count')
print('detach\nquit')
