#include "apu_xaudio2.h"
#include <assert.h>
#include <SDL.h>
int main(void) {
    int16_t samples[256][2]={{123,-123}};
    SDL_setenv("SDL_AUDIODRIVER","dummy",1);
    assert(!xa2_is_active()); assert(xa2_init()); assert(xa2_init());
    assert(xa2_is_active() && xa2_get_buffer_size()==256);
    assert(!xa2_submit_samples(NULL,256));
    assert(!xa2_submit_samples((int16_t*)samples,-1));
    assert(!xa2_submit_samples((int16_t*)samples,257));
    assert(xa2_submit_samples((int16_t*)samples,256));
    xa2_shutdown(); assert(!xa2_is_active()); xa2_shutdown();
    assert(xa2_init()); xa2_shutdown(); return 0;
}
