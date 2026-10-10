#!/usr/bin/env python3
"""Read opt-in GPU batch/fallback counters without modifying the game."""
import argparse,ctypes,json,time
from pathlib import Path
from process_addresses import process_symbols
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('pid',type=int);p.add_argument('--binary',type=Path,required=True)
p.add_argument('--samples',type=int,default=1);p.add_argument('--interval',type=float,default=5)
a=p.parse_args()
if a.samples<1 or a.interval<.01:p.error('invalid sampling interval/count')
names=('ssx_gpu_geometry_batches','ssx_gpu_geometry_fallbacks','s_geometry_cpu_retries')
symbols=process_symbols(a.pid,a.binary,names)
class Iovec(ctypes.Structure):_fields_=[('base',ctypes.c_void_p),('length',ctypes.c_size_t)]
libc=ctypes.CDLL(None,use_errno=True)
libc.process_vm_readv.argtypes=[ctypes.c_int,ctypes.POINTER(Iovec),ctypes.c_ulong,ctypes.POINTER(Iovec),ctypes.c_ulong,ctypes.c_ulong]
libc.process_vm_readv.restype=ctypes.c_ssize_t
for sample in range(a.samples):
 result={'time':time.time()}
 for name in names:
  value=ctypes.c_uint64();local=Iovec(ctypes.addressof(value),8);remote=Iovec(symbols[name],8)
  if libc.process_vm_readv(a.pid,ctypes.byref(local),1,ctypes.byref(remote),1,0)!=8:raise OSError(ctypes.get_errno(),name)
  result[name]=value.value
 print(json.dumps(result),flush=True)
 if sample+1<a.samples:time.sleep(a.interval)
