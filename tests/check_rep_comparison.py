"""Execute lifted REP comparisons against x86 ZF, count and pointer results."""
import argparse, json, subprocess, sys, tempfile
from pathlib import Path
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--cc',default='cc')
args=parser.parse_args()
root = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(root/'third_party/xboxrecomp'))
from tools.recomp.disasm import BasicBlock, Instruction, Operand
from tools.recomp.lifter import Lifter, lift_basic_block

pieces = [r'''
#include <stdint.h>
#include <stdio.h>
#define MEM8(p) (*(uint8_t *)(uintptr_t)(p))
#define MEM16(p) (*(uint16_t *)(uintptr_t)(p))
#define MEM32(p) (*(uint32_t *)(uintptr_t)(p))
#define LO8(x) ((uint8_t)(x))
#define LO16(x) ((uint16_t)(x))
#define RECOMP_DF_STEP(n) (reverse ? -(n) : (n))
#define TEST_NZ(a,b) (((a)&(b))!=0)
#define TEST_Z(a,b) (((a)&(b))==0)
#define CMP_EQ(a,b) ((a)==(b))
typedef struct {unsigned branch; size_t count; intptr_t source,target;} Result;
static unsigned failures,cases;
''']
names=[]
for operation in ['cmpsw','cmpsd','scasw','scasd']:
 for prefix in ['repe','repne']:
  for branch in ['je','jne']:
   name=f'{prefix}_{operation}_{branch}';names.append(name)
   scan=Instruction(0,2,f'{prefix} {operation}','', '')
   width=4 if operation.endswith('d') else 2
   scan.operands=[Operand(type='mem',mem_base='esi',mem_size=width) if operation.startswith('cmps') else Operand(type='reg',reg='eax' if width==4 else 'ax'),Operand(type='mem',mem_base='edi',mem_size=width)]
   jump=Instruction(2,2,branch,'0x10','');jump.jump_target=0x10
   lifter=Lifter();lifter.func_start=0;lifter.func_end=0x20
   op=Operand(type='reg',reg='edx')
   lifted,_=lift_basic_block(lifter,BasicBlock(start=0,instructions=[scan,jump]),('test',[op,op]))
   body='\n'.join(lifted)
   pieces.append(f'''static Result lifted_{name}(void *source,void *target,size_t count,unsigned seed,int reverse) {{
 uintptr_t esi=(uintptr_t)source,edi=(uintptr_t)target; size_t ecx=count;
 uint32_t eax=5,edx=seed,_fa=seed,_fb=seed; int _flags=0;
 {body}
 return (Result){{0,ecx,(intptr_t)esi,(intptr_t)edi}};
 loc_00000010: return (Result){{1,ecx,(intptr_t)esi,(intptr_t)edi}};
}}
static Result native_{name}(void *source,void *target,size_t count,unsigned seed,int reverse) {{
 uintptr_t esi=(uintptr_t)source,edi=(uintptr_t)target;size_t ecx=count; unsigned char z;
 __asm__ volatile("test %[reverse],%[reverse]\\n\\tjz 1f\\n\\tstd\\n\\tjmp 2f\\n1: cld\\n2: test %[seed],%[seed]\\n\\t{prefix} {'cmpsl' if operation=='cmpsd' else 'scasl' if operation=='scasd' else operation}\\n\\tsete %[z]\\n\\tcld"
 : [z] "=q"(z), "+S"(esi), "+D"(edi), "+c"(ecx)
 : [seed] "r"(seed),[reverse] "r"(reverse),"a"(5) : "cc","memory");
 return (Result){{{'z' if branch=='je' else '!z'},ecx,(intptr_t)esi,(intptr_t)edi}};
}}
''')
pieces.append('int main(void) { uint32_t a[64],b[64];\n')
for name in names:
 step=4 if name.split('_')[1].endswith('d') else 2
 pieces.append(f'''for(int reverse=0;reverse<2;reverse++)for(unsigned seed=0;seed<2;seed++)for(unsigned n=0;n<=26;n++)for(unsigned mismatch=0;mismatch<4;mismatch++) {{
 for(unsigned i=0;i<64;i++)a[i]=b[i]=0x00050005;
 unsigned index=mismatch==1?0:mismatch==2?n/2:n?n-1:0;
 if(mismatch && n) {{ if({step}==4)((uint32_t*)b)[index]=7;else ((uint16_t*)b)[index]=7; }}
 if({step}==4)for(unsigned i=0;i<64;i++)a[i]=b[i]=(i==index && mismatch && n)?7:5;
 if({step}==4)for(unsigned i=0;i<64;i++)a[i]=5;
 void *s=(uint8_t*)a+(reverse && n?(n-1)*{step}:0),*d=(uint8_t*)b+(reverse && n?(n-1)*{step}:0);
 Result x=lifted_{name}(s,d,n,seed,reverse),y=native_{name}(s,d,n,seed,reverse);cases++;
 if(x.branch!=y.branch || x.count!=y.count || x.source!=y.source || x.target!=y.target) {{
  if(failures<8)fprintf(stderr,"{name} n=%u seed=%u reverse=%d mismatch=%u branch=%u/%u\\n",n,seed,reverse,mismatch,x.branch,y.branch);
  failures++;
 }}
}}
''')
pieces.append('printf("cases=%u failures=%u\\n",cases,failures);return failures?1:0;}\n')
folder=tempfile.TemporaryDirectory(prefix='ssx-rep-compare-')
source=Path(folder.name)/'fixture.c';source.write_text('\n'.join(pieces))
results=[]
for compiler in [args.cc]:
 binary=Path(folder.name)/'fixture'
 subprocess.run([compiler,'-O2','-w',str(source),'-o',str(binary)],check=True)
 run=subprocess.run([str(binary)],capture_output=True,text=True)
 results.append({'compiler':compiler,'exit':run.returncode,'stdout':run.stdout,'stderr':run.stderr})
print(json.dumps(results,indent=2))
sys.exit(any(r['exit'] for r in results))
