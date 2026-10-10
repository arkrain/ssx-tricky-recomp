#include <assert.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
typedef int32_t LONG;
#include "kernel/diagnostic_counter.h"
static volatile LONG counter;
static void *increment(void *unused)
{
    (void)unused;
    for (int i = 0; i < 100000; ++i) diagnostic_counter_increment(&counter);
    return NULL;
}
static void run(void)
{
    pthread_t threads[8];
    for (int i = 0; i < 8; ++i) assert(!pthread_create(&threads[i], NULL, increment, NULL));
    for (int i = 0; i < 8; ++i) assert(!pthread_join(threads[i], NULL));
}
int main(void)
{
    assert(diagnostic_counter_increment(&counter) == 1);
    counter = 0; run(); assert(diagnostic_counter_read(&counter) == 800000);
    counter = INT32_MAX - 400000; run();
    assert(diagnostic_counter_read(&counter) == INT32_MAX);
    for (int i = 0; i < 100; ++i) assert(diagnostic_counter_increment(&counter) == INT32_MAX);
    puts("Concurrent exact counting and signed-boundary saturation pass");
}
