#!/usr/bin/env python3
from pathlib import Path
import argparse,re,json
from capstone import *
from capstone.x86 import X86_OP_IMM
parser=argparse.ArgumentParser(description='Decode unresolved direct targets and their original incoming branches for manual review.')
parser.add_argument('--output',type=Path,required=True)
args=parser.parse_args()
if args.output.exists():raise SystemExit('Refusing to overwrite an existing audit report')
root=Path(__file__).resolve().parents[1];b=(root/'game/default.xbe').read_bytes();sections=json.loads((root/'game/ssx_analysis.json').read_text())['sections']
def code(va,n=80):
 for s in sections:
  base=int(s['virtual_addr'],16);raw=int(s['raw_addr'],16)
  if 'X' in s['flags'] and base<=va<base+s['raw_size']:return b[raw+va-base:raw+va-base+min(n,base+s['raw_size']-va)]
 return b''
m=Cs(CS_ARCH_X86,CS_MODE_32);m.detail=True
incoming={}
for x in json.loads((root/'third_party/xboxrecomp/tools/disasm/output/xrefs.json').read_text()):
 if x['type'] not in ('jump','cond_jump','call'):continue
 src=int(x['from'],16);dst=int(x['to'],16);i=next(m.disasm(code(src,15),src),None)
 if i and i.operands and i.operands[0].type==X86_OP_IMM and i.operands[0].imm==dst:incoming.setdefault(dst,[]).append(src)
# Include references found while translating jump-table-only blocks, whose
# original direct branches are absent from the disassembler's xref inventory.
for path in (root/'src/recomp/gen').glob('recomp_*.c'):
 text=path.read_text();blocks=list(re.finditer(r'loc_([0-9A-F]{8}): ;',text))
 for n,block in enumerate(blocks):
  start=int(block.group(1),16);end=blocks[n+1].start() if n+1<len(blocks) else len(text)
  targets={int(x,16) for x in re.findall(r'sub_([0-9A-F]{8})\(\)',text[block.end():end])}
  if not targets:continue
  for ins in m.disasm(code(start,192),start):
   if ins.mnemonic.startswith('j') or ins.mnemonic=='call':
    if ins.operands and ins.operands[0].type==X86_OP_IMM and ins.operands[0].imm in targets:
     dst=ins.operands[0].imm
     if ins.address not in incoming.get(dst,[]):incoming.setdefault(dst,[]).append(ins.address)
   if ins.mnemonic in ('ret','jmp'):break
# A target already inside another emitted body may be a local branch in a
# duplicate, misidentified function. Seeding it independently can split working
# code. Include ownership evidence so the reviewer can distinguish that case.
spans=[]
for path in (root/'src/recomp/gen').glob('recomp_*.c'):
 for start,end in re.findall(r'Original: 0x([0-9A-F]+) - 0x([0-9A-F]+)',path.read_text()):
  spans.append((int(start,16),int(end,16)))
results=[]
for h in re.findall(r'void sub_([0-9A-F]+)',(root/'src/recomp/gen/recomp_stubs_unresolved.c').read_text()):
 va=int(h,16)
 if va not in incoming or not code(va):continue
 seq=[]
 for i in m.disasm(code(va),va):
  seq.append({'address':hex(i.address),'bytes':i.bytes.hex(),'instruction':f'{i.mnemonic} {i.op_str}'.strip()})
  if i.mnemonic in ('ret','jmp','int3') or len(seq)>=12:break
 result={'start':hex(va),'sources':[hex(s) for s in incoming[va]],'containing_bodies':[hex(start) for start,end in spans if start < va < end],'instructions':seq};results.append(result)
 print(hex(va),result['sources'], '; '.join(x['instruction'] for x in seq))
args.output.write_text(json.dumps(results,indent=2)+'\n')
