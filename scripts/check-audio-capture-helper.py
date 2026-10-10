#!/usr/bin/env python3
"""Asset-free regression for stale/partial diagnostic recording rejection."""
import json,subprocess,sys,tempfile,threading,time
from pathlib import Path
root=Path(__file__).resolve().parents[1]
results=[]
for scenario in ('stale','partial','fresh','exited'):
 with tempfile.TemporaryDirectory(prefix='ssx-capture-check-') as directory:
  d=Path(directory);live=d/'live.pcm';trigger=d/'trigger';out=d/'out.pcm'
  live.write_bytes(bytes(480000*4))
  child=subprocess.Popen([sys.executable,'-c','import time;time.sleep(30)'])
  def writer():
   until=time.monotonic()+3
   while not trigger.exists() and time.monotonic()<until:time.sleep(.01)
   if not trigger.exists():return
   trigger.unlink()
   if scenario=='fresh':live.write_bytes(bytes([1,0,2,0])*480000)
   if scenario=='partial':live.write_bytes(bytes(4096))
  thread=threading.Thread(target=writer)
  try:
   if scenario=='exited':child.terminate();child.wait()
   else:thread.start()
   command=[sys.executable,str(root/'scripts/capture-triggered-audio.py'),str(child.pid),
    '--binary',sys.executable,'--trigger',str(trigger),'--live',str(live),'--output',str(out),'--timeout','.5']
   result=subprocess.run(command,capture_output=True,text=True)
   if scenario=='fresh':
    assert result.returncode==0 and out.read_bytes()==bytes([1,0,2,0])*480000
   else:assert result.returncode!=0 and not out.exists()
   results.append({'scenario':scenario,'passed':True,'returncode':result.returncode,'message':(result.stdout+result.stderr).strip()})
  finally:
   if child.poll() is None:child.terminate();child.wait()
   if thread.ident is not None:thread.join()
print(json.dumps(results,indent=2))
