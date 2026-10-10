#!/usr/bin/env python3
"""Read-only, discontinuous APU output samples for mute/level diagnostics.

The monitor offset must be measured from the exact build's MCPXAPUState.
These are statistical snapshots, not a continuous audio recording.
The monitor buffer is cleared immediately after output submission. Zero reads
therefore do not measure audible silence; use RECOMP_AUDIO_DUMP for continuity.
"""
import argparse
import ctypes as c
import json
import math
import struct
import subprocess
import time

p=argparse.ArgumentParser(description=__doc__)
p.add_argument('pid',type=int)
p.add_argument('--binary',required=True)
p.add_argument('--monitor-offset',required=True,type=lambda s:int(s,0))
p.add_argument('--seconds',type=float,default=3)
a=p.parse_args()
class Iov(c.Structure):
    _fields_=[('base',c.c_void_p),('size',c.c_size_t)]
lib=c.CDLL(None,use_errno=True)
def read(address,size):
    b=c.create_string_buffer(size);local=Iov(c.addressof(b),size);remote=Iov(address,size)
    n=lib.process_vm_readv(a.pid,c.byref(local),1,c.byref(remote),1,0)
    if n!=size:raise OSError(c.get_errno(),'process_vm_readv failed')
    return b.raw
from process_addresses import process_symbols
symbol=process_symbols(a.pid,a.binary,('g_state',))['g_state']
state=struct.unpack('<Q',read(symbol,8))[0]
if not state:raise RuntimeError('APU is not initialized')
end=time.monotonic()+a.seconds
count=total=peak=nonzero=0
while time.monotonic()<end:
    values=struct.unpack('<512h',read(state+a.monitor_offset,1024))
    count+=len(values);total+=sum(x*x for x in values)
    peak=max(peak,max(abs(x) for x in values));nonzero+=sum(x!=0 for x in values)
    time.sleep(.01)
print(json.dumps({'pid':a.pid,'discontinuous':True,'buffer_cleared_after_submission':True,'seconds':a.seconds,'samples':count,'peak':peak,'rms':math.sqrt(total/count),'nonzero':nonzero}))
