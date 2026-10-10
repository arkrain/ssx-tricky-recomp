"""Monotonic deadlines for coordinated, read-only process sampling."""
import math
import time


def wait_until(deadline):
    delay = deadline - time.monotonic()
    if delay > 0:
        time.sleep(delay)


def sampling_start(deadline=None):
    now = time.monotonic()
    if deadline is None:
        return now
    if not math.isfinite(deadline) or deadline < 0:
        raise ValueError('sampling start must be a finite nonnegative monotonic time')
    if now > deadline:
        raise ValueError('sampling start deadline passed during setup')
    wait_until(deadline)
    return deadline
