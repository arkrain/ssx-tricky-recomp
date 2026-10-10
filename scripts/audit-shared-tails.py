#!/usr/bin/env python3
"""Audit unresolved branch targets for strict original-x86 epilogues; no source edits."""
import argparse,json,re
from pathlib import Path
from capstone import *
from capstone.x86 import X86_OP_IMM
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--output',type=Path,required=True,help='New audit report; frozen reports are never overwritten')
parser.add_argument('--include-volatile-pops',action='store_true')
args=parser.parse_args()
if args.output.exists():raise SystemExit('Refusing to overwrite an existing audit report')
allowed_pops=('edi','esi','ebp','ebx')+(('eax','ecx','edx') if args.include_volatile_pops else ())
root=Path(__file__).resolve().parents[1];xbe=(root/'game/default.xbe').read_bytes()
sections=json.loads((root/'game/ssx_analysis.json').read_text())['sections']
def code(va,count=64):
 for s in sections:
  base=int(s['virtual_addr'],16); raw=int(s['raw_addr'],16)
  if base<=va<base+s['raw_size'] and 'X' in s['flags']:
   return xbe[raw+va-base:raw+va-base+min(count,base+s['raw_size']-va)]
 return b''
md=Cs(CS_ARCH_X86,CS_MODE_32);md.detail=True
xrefs=json.loads((root/'third_party/xboxrecomp/tools/disasm/output/xrefs.json').read_text())
incoming={}
for x in xrefs:
 if x['type'] not in ('cond_jump','jump','call'):continue
 target=int(x['to'],16);source=int(x['from'],16)
 ins=next(md.disasm(code(source,15),source),None)
 if ins and ins.operands and ins.operands[0].type==X86_OP_IMM and ins.operands[0].imm==target:
  incoming.setdefault(target,[]).append(source)
missing=sorted(set(int(x,16) for x in re.findall(r'void sub_([0-9A-F]+)',(root/'src/recomp/gen/recomp_stubs_unresolved.c').read_text())))
results=[]
for address in missing:
 if address not in incoming:continue
 seq=[]
 for ins in md.disasm(code(address,32),address):
  seq.append(f'{ins.mnemonic} {ins.op_str}'.strip())
  if ins.mnemonic=='ret':
   if len(seq)>1:results.append({'start':f'0x{address:08X}','sources':[f'0x{v:08X}' for v in incoming[address]],'instructions':seq})
   break
  if not (ins.mnemonic=='pop' and ins.op_str in allowed_pops or ins.mnemonic=='leave' or ins.mnemonic=='add' and ins.op_str.startswith('esp, ')):
   break
args.output.write_text(json.dumps(results,indent=2)+'\n')
print('Verified simple epilogues:',len(results))
for r in results[:15]:print(r)
