#!/usr/bin/env python3
"""Execute recovered original epilogues from the real generated game objects.
Checks stack movement and EBX/ESI/EDI restoration against the decoded instructions.
EBP is caller-local in the translation, so its TLS shadow is not an output check.
"""
import argparse,json,shlex,subprocess,tempfile
from pathlib import Path
root=Path(__file__).resolve().parents[1]
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('--build-dir',type=Path,default=root/'build/linux-release')
p.add_argument('--audit',type=Path,action='append',help='Additional audit records to execute')
a=p.parse_args();build=a.build_dir.resolve()
records=json.loads((root/'reports/verified-epilogue-audit58.json').read_text())
for audit in a.audit or []:records+=json.loads(audit.read_text())
source=['#include "recomp_funcs.h"','#include <stdlib.h>','#include <stdio.h>',
'int main(void) { uint32_t *ram=malloc(0x800000); if(!ram)return 2;',
'for(unsigned i=0;i<0x200000;i++)ram[i]=0x51000000+i;',
'g_xbox_mem_offset=(ptrdiff_t)ram; int failed=0;']
for item in records:
 regs={'esp':0x1000,'ebp':0x1800,'ebx':0x1111,'esi':0x2222,'edi':0x3333,'eax':0x4444,'ecx':0x5555,'edx':0x6666}
 for line in item['instructions']:
  mnemonic,*op=line.split(' ',1);operand=op[0] if op else ''
  if mnemonic=='pop':regs[operand]=0x51000000+regs['esp']//4;regs['esp']+=4
  elif mnemonic=='leave':regs['esp']=regs['ebp']+4
  elif mnemonic=='mov':
   dst,src=operand.split(', ');regs[dst]=regs[src]
  elif mnemonic=='xor':
   dst,src=operand.split(', ');assert dst==src;regs[dst]=0
  elif mnemonic=='add':regs['esp']+=int(operand.split(', ')[1],0)
  elif mnemonic=='ret':regs['esp']+=4+(int(operand,0) if operand else 0)
  else:raise SystemExit('Unexpected instruction '+line)
 address=int(item['start'],16)
 source+=['g_esp=0x1000;g_ebp=g_seh_ebp=0x1800;g_ebx=0x1111;g_esi=0x2222;g_edi=0x3333;g_eax=0x4444;g_ecx=0x5555;g_edx=0x6666;',f'sub_{address:08X}();']
 cond=' || '.join(f'g_{reg} != 0x{regs[reg]:x}u' for reg in ('esp','ebx','esi','edi','eax','ecx','edx'))
 source+=[f'if({cond}) {{fprintf(stderr,"epilogue {address:08X} failed: esp=%x ebx=%x esi=%x edi=%x\\n",g_esp,g_ebx,g_esi,g_edi);failed++;}}']
source += [f'printf("{len(records)} generated shared epilogues: %s\\n",failed?"FAILED":"passed");free(ram);return failed!=0;}}']
commands=subprocess.check_output(['ninja','-C',str(build),'-t','commands','ssx_tricky'],text=True)
link=shlex.split(commands.splitlines()[-1])
if link[:2]!=[':','&&'] or link[-2:]!=['&&',':']:raise SystemExit('Unexpected link command')
link=[v for v in link[2:-2] if not v.startswith('-Wl,--dependency-file=')]
with tempfile.TemporaryDirectory(prefix='ssx-tails-') as folder:
 folder=Path(folder);c=folder/'probe.c';obj=folder/'probe.o';exe=folder/'probe';c.write_text('\n'.join(source)+'\n')
 subprocess.run([link[0],'-I'+str(root/'src/recomp/gen'),'-c',str(c),'-o',str(obj)],check=True)
 link[link.index('CMakeFiles/ssx_tricky.dir/src/main.c.o')]=str(obj);link[link.index('-o')+1]=str(exe)
 subprocess.run(link,cwd=build,check=True);subprocess.run([str(exe)],check=True)
