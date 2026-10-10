#!/usr/bin/env python3
"""Stress the actual bridge DPC enqueue/drain code with concurrent producers."""
from pathlib import Path
import argparse
import subprocess
import tempfile
root = Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser(description=__doc__)
parser.add_argument('--source',type=Path,default=root/'third_party/xboxrecomp/src/kernel/kernel_bridge.c')
parser.add_argument('--cc',default='/usr/bin/gcc')
args=parser.parse_args()
s = args.source.read_text()
queue = s[s.index('#define XBOX_MAX_PENDING_DPC'):s.index('/* Call a connected interrupt service routine.')]
start = s.index('static void kernel_drain_dpcs(void)')
drain = s[start:s.index('/* ── KeInitializeDpc', start)]
prelude = r'''
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
typedef int32_t LONG;
static _Thread_local uint32_t args[3], g_eax;
#define STACK_ARG(i) args[i]
static LONG InterlockedCompareExchange(volatile LONG *p,LONG n,LONG old) {
    __atomic_compare_exchange_n(p,&old,n,0,__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST); return old;
}
static LONG InterlockedExchange(volatile LONG *p,LONG n) { return __atomic_exchange_n(p,n,__ATOMIC_SEQ_CST); }
static void Sleep(unsigned n) { (void)n; sched_yield(); }
#define COUNT 200000
static unsigned seen[2][COUNT];
static atomic_int done;
static int kernel_run_dpc(uint32_t dpc,uint32_t a,uint32_t b) {
    assert(dpc>=1 && dpc<=2 && a<COUNT && b==(a^0xa5a5a5a5u));
    assert(seen[dpc-1][a]++==0); return 1;
}
'''
main = r'''
static void *producer(void *p) {
    args[0]=(uint32_t)(uintptr_t)p;
    for(unsigned i=0;i<COUNT;i++) {
        args[1]=i;args[2]=i^0xa5a5a5a5u;
        do { bridge_KeInsertQueueDpc(); if(!g_eax)sched_yield(); } while(!g_eax);
    }
    atomic_fetch_add(&done,1);return NULL;
}
int main(void) {
    pthread_t a,b;
    assert(!pthread_create(&a,NULL,producer,(void*)1));
    assert(!pthread_create(&b,NULL,producer,(void*)2));
    while(atomic_load(&done)!=2) {kernel_drain_dpcs();sched_yield();}
    pthread_join(a,NULL);pthread_join(b,NULL);kernel_drain_dpcs();
    for(unsigned p=0;p<2;p++)for(unsigned i=0;i<COUNT;i++)assert(seen[p][i]==1);
    puts("400000 concurrent DPC callbacks delivered exactly once");
}
'''
with tempfile.TemporaryDirectory(prefix='ssx-dpc-') as tmp:
    source=Path(tmp)/'check.c';binary=Path(tmp)/'check'
    source.write_text(prelude+queue+drain+main)
    subprocess.run([args.cc,'-std=c11','-O2','-pthread',str(source),'-o',str(binary)],check=True)
    with open('/dev/null','w') as errors:
        subprocess.run([str(binary)],stderr=errors,check=True,timeout=30)
