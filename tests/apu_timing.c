#include "apu_timing.h"
#include <assert.h>
#include <stdio.h>
int main(void)
{
    /* Independent sample-count oracle: a full day's output has no drift. */
    int64_t origin = INT64_C(365) * 86400 * 1000000 + 12345;
    int64_t deadline = 0;
    apu_deadline_prepare(&deadline, origin);
    for (int64_t frame = 1; frame <= INT64_C(48000) * 86400 / 256; ++frame) {
        apu_deadline_advance(&deadline);
        int64_t samples = frame * 256;
        assert((deadline - origin * 3) * 48000 == samples * 3000000);
        int64_t observed_us = deadline / 3;
        int64_t saved = deadline;
        apu_deadline_prepare(&deadline, observed_us);
        assert(deadline == saved);
    }
    assert(deadline == (origin + INT64_C(86400) * 1000000) * 3);
    /* Millisecond waits retain the fractional deadline, including boundaries. */
    deadline = origin * 3 + 16000;
    /* Independently model throttle's rechecking timed-wait loop. It must
     * never release a block before its rational sample deadline, even when
     * starting less than one millisecond before it. */
    for (int elapsed = 0; elapsed <= 6334; ++elapsed) {
        int64_t now = origin + elapsed;
        for (int tries = 0; tries < 100; ++tries) {
            int64_t wait = apu_deadline_remaining_ms(deadline, now);
            if (wait <= 0) break;
            now += wait * 1000;
            assert(tries < 99);
        }
        assert(now * 3 >= deadline);
    }
    assert(apu_deadline_remaining_ms(deadline, origin) == 6);
    assert(apu_deadline_remaining_ms(deadline, origin + 4333) == 2);
    assert(apu_deadline_remaining_ms(deadline, origin + 4334) == 1);
    assert(apu_deadline_remaining_ms(deadline, origin + 5334) == 0);
    assert(apu_deadline_remaining_ms(deadline, origin + 6334) == -1);
    /* Preserve a slightly late deadline; abandon catch-up after a full block. */
    int64_t saved = deadline;
    apu_deadline_prepare(&deadline, origin + 10666);
    assert(deadline == saved);
    apu_deadline_prepare(&deadline, origin + 10667);
    assert(deadline == (origin + 10667) * 3);
    apu_deadline_prepare(&deadline, origin + 60000000);
    assert(deadline == (origin + 60000000) * 3);
    for (int n = 0; n < 3; ++n) apu_deadline_advance(&deadline);
    assert(deadline == (origin + 60000000 + 16000) * 3);
    puts("24-hour sample-clock oracle, long uptime, fractional waits and late recovery pass");
}
