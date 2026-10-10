/* The full kernel's TLS must fit SDL2's 64 KiB queued-audio workers. */
#include <stdint.h>
#include <pthread.h>
#define main audio_lifecycle_main
#include "audio_output.c"
#undef main
typedef void (*recomp_func_t)(void);
recomp_func_t recomp_lookup(uint32_t va) { (void)va;return NULL; }
extern recomp_func_t recomp_lookup_kernel(uint32_t);
static void *small_worker(void *argument) { return argument; }
int main(void) {
    /* Pull in the real thunk bridge, including its thread-local storage. */
    assert(!recomp_lookup_kernel(0));
    pthread_attr_t attributes;pthread_t thread;void *result=NULL;
    assert(!pthread_attr_init(&attributes));
    assert(!pthread_attr_setstacksize(&attributes,64*1024));
    assert(!pthread_create(&thread,&attributes,small_worker,&attributes));
    assert(!pthread_join(thread,&result) && result==&attributes);
    assert(!pthread_attr_destroy(&attributes));
    return audio_lifecycle_main();
}
