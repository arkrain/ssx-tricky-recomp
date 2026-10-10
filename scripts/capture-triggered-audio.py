#!/usr/bin/env python3
"""Archive a fresh, complete diagnostic audio capture from a verified live game.

The game must already use matching RECOMP_AUDIO_DUMP/TRIGGER paths. This helper
never accepts a stale dump as new evidence and never overwrites an archive.
"""
import argparse,os,time
from pathlib import Path
p=argparse.ArgumentParser(description=__doc__)
p.add_argument('pid',type=int);p.add_argument('--binary',type=Path,required=True)
p.add_argument('--trigger',type=Path,required=True);p.add_argument('--live',type=Path,required=True)
p.add_argument('--seconds',type=int,default=10,help='Must match RECOMP_AUDIO_SECONDS (default10)')
p.add_argument('--output',type=Path,required=True);p.add_argument('--timeout',type=float,default=30)
a=p.parse_args()
if a.timeout<=0 or not 1<=a.seconds<=60:p.error('timeout must be positive; seconds must be1..60')
expected=a.seconds*48000*4
if a.output.exists():raise SystemExit('Output exists; refusing overwrite')
if a.trigger.exists():raise SystemExit('Unconsumed trigger exists; inspect before rearming')
def alive():
 try:
  return os.path.samefile(a.binary,f'/proc/{a.pid}/exe') and Path(f'/proc/{a.pid}/stat').read_text().rsplit(')',1)[1].split()[0]!='Z'
 except (FileNotFoundError,ProcessLookupError):return False
if not alive():raise SystemExit('Selected game is not live with the supplied binary')
old=a.live.stat().st_mtime_ns if a.live.exists() else None
with a.trigger.open('xb'):pass
end=time.monotonic()+a.timeout
while time.monotonic()<end:
 if not alive():raise SystemExit('Game exited before capture completed; no archive written')
 if not a.trigger.exists() and a.live.exists():
  info=a.live.stat()
  if info.st_size==expected and info.st_mtime_ns!=old:
   data=a.live.read_bytes()
   if len(data)==expected and a.live.stat().st_mtime_ns==info.st_mtime_ns:
    with a.output.open('xb') as f:f.write(data)
    print(f'Archived fresh{a.seconds}-second48kHz stereo capture: {a.output}');break
 time.sleep(.1)
else:raise SystemExit('Timed out waiting for a fresh complete capture; no archive written')
