#!/usr/bin/env python3
"""Replay a recorded AI race route using ordinary controller input only.

Linux diagnostic for the supported US retail image. Reads position telemetry
through process_vm_readv and writes hexadecimal button bits to the configured
RECOMP_TEST_INPUT_FILE. It never writes guest memory. Use an isolated test save
and the exact running binary. Record routes with live-game-state.py and stop
at race completion; --rider selects an AI slot in that recording, while the
live human rider is discovered independently after each heat's roster reorder.
"""
import argparse,ctypes,struct,subprocess,json,math,time,signal
from pathlib import Path
from probe_input import write_input

def mode_airborne(mode,upright,fallback):
 # Observed US-retail rider modes, not a general collision/contact flag.
 # See reports/progression203-validation.md for source and live evidence.
 if mode==1:return True
 if mode in (2,4) and upright>.8:return False
 return fallback

p=argparse.ArgumentParser(description=__doc__);p.add_argument('pid',type=int);p.add_argument('--binary',required=True);p.add_argument('--route',required=True);p.add_argument('--input',required=True);p.add_argument('--rider',type=int,default=1);p.add_argument('--player-base',type=lambda v:int(v,0));p.add_argument('--seconds',type=float,default=600);p.add_argument('--wait-at-start',action='store_true',help='Wait up to 30 seconds for a restart to place the human near the recorded start');p.add_argument('--growing-route',action='store_true',help='Read appended route samples while recording continues')
p.add_argument('--grab-rising',action='store_true',help='Try Y grabs only while rising upright, releasing before the descent')
p.add_argument('--grab-min-speed',type=float,default=200,help='Minimum upward normal velocity for a rising grab (default:200); higher values reserve grabs for larger ascents')
p.add_argument('--match-route-speed',action='store_true',help='Brake on the ground when more than20 percent above recorded rider speed, with a1200-unit floor')
p.add_argument('--wide-rejoin',action='store_true',help='Aim farther down the route when displaced from a shortcut')
p.add_argument('--loop-route',action='store_true',help='Retain local route progress after resets to distinguish repeated laps')
p.add_argument('--resume-index',type=int,help='Resume a previous driver log index without globally matching another lap')
p.add_argument('--stall-timeout',type=float,default=12,help='Seconds without route progress before pressing reset (default: 12)')
p.add_argument('--hold-crouch',action='store_true',help='Hold A through the route to crouch without deliberately releasing charged jumps')
p.add_argument('--steer-full-angle',type=float,help='Pulse ground steering in proportion to heading error; full steering at this angle in radians (omitted: original full-stick steering)')
p.add_argument('--analog-steering',action='store_true',help='Use signed probe stick values instead of full-stick pulses; requires build164 or later')
p.add_argument('--landing-speed',type=float,default=60,help='Normal-velocity threshold for three consecutive upright landing samples (default:60); airborne entry uses at least1.5 times this threshold')
p.add_argument('--rider-mode-landing',action='store_true',help='Use observed US-retail rider modes1/2/4 for landing detection; other modes retain the velocity heuristic')
a=p.parse_args()
if a.seconds<=0 or a.rider<0:p.error('seconds must be positive and rider nonnegative')
if not math.isfinite(a.stall_timeout) or a.stall_timeout<=0:p.error('stall timeout must be finite and positive')
if a.steer_full_angle is not None and (not math.isfinite(a.steer_full_angle) or a.steer_full_angle<=0):p.error('steer full angle must be finite and positive')
if not math.isfinite(a.landing_speed) or a.landing_speed<=0:p.error('landing speed must be finite and positive')
if not math.isfinite(a.grab_min_speed) or a.grab_min_speed<=0:p.error('grab minimum speed must be finite and positive')
def stop(signum,frame):
 raise KeyboardInterrupt
signal.signal(signal.SIGTERM,stop)
path=[];path_speeds=[]
route_file=open(a.route)
def extend_route():
 while True:
  offset=route_file.tell();line=route_file.readline()
  if not line:break
  if not line.endswith('\n'):
   route_file.seek(offset);break
  try:
   recorded=json.loads(line)['riders'][a.rider];r=recorded['position'][:3]
  except (KeyError,IndexError,json.JSONDecodeError):continue
  if not path or math.dist(r,path[-1])>100:
   path.append(r)
   velocity=recorded.get('velocity',[])
   reference_speed=math.hypot(*velocity[:2]) if len(velocity)>=2 else None
   path_speeds.append(reference_speed if reference_speed is not None and math.isfinite(reference_speed) else None)
extend_route()
if len(path)<100:raise SystemExit('Route too short')
if a.resume_index is not None and not 0<=a.resume_index<len(path):p.error('resume index is outside the recorded route')

class Iovec(ctypes.Structure):_fields_=[('base',ctypes.c_void_p),('length',ctypes.c_size_t)]
libc=ctypes.CDLL(None,use_errno=True);libc.process_vm_readv.argtypes=[ctypes.c_int,ctypes.POINTER(Iovec),ctypes.c_ulong,ctypes.POINTER(Iovec),ctypes.c_ulong,ctypes.c_ulong];libc.process_vm_readv.restype=ctypes.c_ssize_t
def read(address,size):
 b=ctypes.create_string_buffer(size);l=Iovec(ctypes.addressof(b),size);r=Iovec(address,size)
 if libc.process_vm_readv(a.pid,ctypes.byref(l),1,ctypes.byref(r),1,0)!=size:raise OSError(ctypes.get_errno(),'read failed')
 return b.raw
from process_addresses import process_symbols
names=('g_xbox_mem_offset','xbox_test_input_version') if a.analog_steering else ('g_xbox_mem_offset',)
symbols=process_symbols(a.pid,a.binary,names)
if a.analog_steering and struct.unpack('<I',read(symbols['xbox_test_input_version'],4))[0]<2:
 raise SystemExit('The running binary does not support analog probe input')
base=struct.unpack('<Q',read(symbols['g_xbox_mem_offset'],8))[0]
def u32(addr):return struct.unpack('<I',read(base+addr,4))[0]
def vec(addr):return struct.unpack('<4f',read(base+addr,16))
f=Path(a.input)
def control(mask,axis=0):
 write_input(f,mask,(axis,0,0,0) if a.analog_steering else None)
start=time.monotonic();index=a.resume_index;last_log=0;slow=0;last_pos=None;airborne=False;ground_ticks=0;waiting=a.wait_at_start;ready_deadline=start+30
progress_index=-1;progress_time=start
try:
 while time.monotonic()-start<a.seconds:
  if a.growing_route:extend_route()
  manager=u32(u32(u32(0x1e3c7c)+0x72c)+0x1c);count=u32(manager+0x88)
  if count>8:raise ValueError('Unexpected rider count')
  riders=[u32(manager+0xc4+4*i) for i in range(count)]
  rider=a.player_base or next((r for r in riders if u32(r)==0x189ed0 and u32(r+0x38)==0),0)
  if rider not in riders:
   if waiting and time.monotonic()<ready_deadline:time.sleep(.1);continue
   break
  pos=vec(rider+0x170);vel=vec(rider+0x180);q=vec(rider+0x1a0);normal=vec(rider+0x2f0)
  if waiting:
   if math.dist(pos[:3],path[0])>5000:
    if time.monotonic()>ready_deadline:raise TimeoutError('Rider did not reach recorded start')
    time.sleep(.1);continue
   waiting=False;start=time.monotonic()
  if last_pos is not None and math.dist(pos[:3],last_pos)>15000:
   if a.loop_route and math.hypot(pos[0]-last_pos[0],pos[1]-last_pos[1])<5000 and pos[2]-last_pos[2]>15000:
    print(json.dumps({'event':'vertical_reposition','elapsed':time.monotonic()-start,'index':index}),flush=True)
   else:
    print('Stopped on rider discontinuity',flush=True);break
  last_pos=pos[:3]
  lo=0 if index is None else max(0,index-30);hi=len(path) if index is None else min(len(path),index+180)
  index=min(range(lo,hi),key=lambda i:math.dist(pos[:3],path[i]))
  if index>progress_index+3:
   progress_index=index;progress_time=time.monotonic()
  speed=math.hypot(*vel[:2]);ahead=max(1200,min(4000,speed*.9));j=index;distance=0
  off_route=math.dist(pos[:3],path[index])
  if a.wide_rejoin and off_route>2500:ahead=max(ahead,min(10000,off_route*1.25))
  while j+1<len(path) and distance<ahead:distance+=math.dist(path[j],path[j+1]);j+=1
  dx,dy=path[j][0]-pos[0],path[j][1]-pos[1];w,x,y,z=q
  hx,hy=2*(x*y-w*z),1-2*(x*x+z*z)
  error=math.atan2(hx*dy-hy*dx,hx*dx+hy*dy)
  normal_speed=sum(v*n for v,n in zip(vel[:3],normal[:3]))
  up=(2*(x*z+w*y),2*(y*z-w*x),1-2*(x*x+y*y))
  upright=sum(u*n for u,n in zip(up,normal[:3]))
  if abs(normal_speed)>max(120,1.5*a.landing_speed) or upright<.8:
   airborne=True;ground_ticks=0
  elif abs(normal_speed)<a.landing_speed and upright>.8:
   ground_ticks+=1
   if ground_ticks>=3:airborne=False
  else:ground_ticks=0
  pre_override_airborne=airborne
  rider_mode=None
  if a.rider_mode_landing:
   rider_mode=u32(rider+0x454)
   airborne=mode_airborne(rider_mode,upright,airborne)
  key=0x200 if airborne else 0x10200
  reference_speed=path_speeds[index]
  braking=a.match_route_speed and not airborne and reference_speed is not None and speed>max(1200,1.2*reference_speed)
  if braking:key=0x20000
  if a.hold_crouch:key|=0x100
  if a.grab_rising and airborne and normal_speed>a.grab_min_speed and upright>.8:key|=0x800
  if abs(error)>.09 and not airborne:key|=0x40000 if error>0 else 0x80000
  # The diagnostic keyboard mask supplies full stick deflection. Shorter
  # pulses reduce overcorrection on gentle bends without changing the game.
  # Keep pulses at least20ms so the16ms input-file poll can observe them.
  steer_duty=1.0
  if a.steer_full_angle is not None and key&0xc0000:
   steer_duty=min(1.0,max(.2,abs(error)/a.steer_full_angle))
  steer_axis=0
  if a.analog_steering and key&0xc0000:
   magnitude=round(32767*min(1.0,max(.25,abs(error)/(a.steer_full_angle or .5))))
   steer_axis=-magnitude if error>0 else magnitude
   key &= ~0xc0000
   steer_duty=1.0
  slow=slow+.1 if speed<150 else 0
  # Reset sooner when already stationary well away from the recorded route.
  # Retain the longer allowance near it for countdowns and ordinary landings.
  stalled=time.monotonic()-progress_time>a.stall_timeout and index<len(path)-5
  if slow>6 or (slow>2 and off_route>3000) or stalled:
   print(json.dumps({'event':'reset','elapsed':time.monotonic()-start,
                    'distance':off_route,'stationary_seconds':slow,'route_stalled':stalled}),flush=True)
   control(0x20);time.sleep(.6);control(0);slow=0
   if not a.loop_route:index=None
   # Reacquire the route after the reset before logging/indexing it again.
   last_pos=None
   progress_index=-1;progress_time=time.monotonic()
   continue
  else:control(key,steer_axis)
  now=time.monotonic()
  if now-last_log>1:
   print(json.dumps({'elapsed':now-start,'index':index,'target':j,'distance':math.dist(pos[:3],path[index]),'speed':speed,'reference_speed':reference_speed,'braking':braking,'angle':error,'normal_speed':normal_speed,'upright':upright,'airborne':airborne,'rider_mode':rider_mode,'pre_override_airborne':pre_override_airborne,'input':hex(key),'steer_duty':steer_duty,'steer_axis':steer_axis}),flush=True);last_log=now
  if index>=len(path)-5:
   if not a.growing_route:break
   control(0x20000);time.sleep(.2)
  if steer_duty<1.0:
   time.sleep(.1*steer_duty);control(key&~0xc0000);time.sleep(.1*(1-steer_duty))
  else:time.sleep(.1)
except KeyboardInterrupt:
 print('Replay interrupted; releasing input',flush=True)
finally:control(0)
