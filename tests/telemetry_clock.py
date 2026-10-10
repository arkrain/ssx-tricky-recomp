"""Deadline semantics: setup failure, late samples, and no accumulated work delay."""
import sys,unittest
from pathlib import Path
from unittest.mock import patch
sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'scripts'))
import sample_clock as clock
class DeadlineTests(unittest.TestCase):
 def test_work_does_not_shift_later_samples(self):
  now=[2.0];slept=[]
  def sleep(t):slept.append(t);now[0]+=t
  with patch.object(clock.time,'monotonic',lambda:now[0]),patch.object(clock.time,'sleep',sleep):
   start=clock.sampling_start(10)
   observed=[]
   for index,work in enumerate((.2,1.5,.1,.1)):
    clock.wait_until(start+index);observed.append(now[0]);now[0]+=work
   self.assertEqual(observed,[10,11,12.5,13])
   self.assertAlmostEqual(slept[-1],.4)
 def test_setup_must_finish_before_deadline(self):
  with patch.object(clock.time,'monotonic',lambda:10),patch.object(clock.time,'sleep') as sleep:
   for deadline in (9,float('nan'),float('inf'),-1):
    with self.assertRaises(ValueError):clock.sampling_start(deadline)
   sleep.assert_not_called()
   self.assertEqual(clock.sampling_start(),10)
 def test_oversleep_preserves_intended_origin(self):
  now=[2.0]
  def sleep(t):now[0]+=t+.1
  with patch.object(clock.time,'monotonic',lambda:now[0]),patch.object(clock.time,'sleep',sleep):
   self.assertEqual(clock.sampling_start(10),10)
   self.assertEqual(now[0],10.1)
if __name__=='__main__':unittest.main()
